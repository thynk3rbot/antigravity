#!/usr/bin/env python3
"""
ble_instrument.py — LoRaLink-AnyToAny BLE Instrumentation Framework

Connects to the Heltec ESP32-S3 via Nordic UART Service (NUS) BLE profile,
exercises all command groups in rotation, pushes the firmware's dynamic task
scheduler to MAX_DYNAMIC_TASKS=5, and logs structured session metrics to JSON.

Usage:
    python tools/ble_instrument.py [options]
    python tools/ble_instrument.py --device "MyNode" --ip 192.168.1.50
    python tools/ble_instrument.py --no-maximize --interval 5 --dry-run

Options:
    --device        Device name prefix to scan for  (default: HT-LoRa)
    --ip            Device IP for HTTP /api/status polling  (optional)
    --interval      Rotation interval in seconds  (default: 8)
    --no-maximize   Skip SCHED ADD burst at startup
    --dry-run       Simulate BLE writes without connecting
    --log           Session log output path  (default: ble_session.json)

Install dependencies:
    pip install -r tools/requirements.txt
"""
from __future__ import annotations

import argparse
import asyncio
import json
import random
import sys
import time
from collections import deque
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Optional

# ── Rotation strategy persistence ───────────────────────────────────────────
_PREFS_FILE = Path(__file__).parent / ".loralink_prefs.json"
_ROTATION_STRATEGIES = ["sequential", "weighted", "adaptive", "random", "interleave"]

# ── Optional rich import (degrades gracefully to plain print) ────────────────
try:
    from rich.console import Console
    from rich.layout import Layout
    from rich.live import Live
    from rich.panel import Panel
    from rich.table import Table

    RICH = True
    console = Console()
except ImportError:
    RICH = False

    class _FallbackConsole:
        def print(self, *a, **kw):
            # Strip basic rich markup tags before printing
            msg = " ".join(str(x) for x in a)
            for tag in ["bold", "cyan", "green", "red", "yellow", "dim", "italic"]:
                msg = msg.replace(f"[{tag}]", "").replace(f"[/{tag}]", "")
                msg = msg.replace(f"[bold {tag}]", "").replace(f"[/bold {tag}]", "")
            print(msg)

        def log(self, *a, **kw):
            self.print(*a)

    console = _FallbackConsole()  # type: ignore

# ── Hard dependency: bleak ───────────────────────────────────────────────────
try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
    BLEAK = True
except ImportError:
    BLEAK = False
    console.print("ERROR: bleak not installed — run: pip install bleak")
    sys.exit(1)

# ── Optional: aiohttp for HTTP polling ──────────────────────────────────────
try:
    import aiohttp
    AIOHTTP = True
except ImportError:
    AIOHTTP = False


# ════════════════════════════════════════════════════════════════════════════
# 1. BLEConstants
# ════════════════════════════════════════════════════════════════════════════

class BLEConstants:
    """Nordic UART Service UUIDs — from BLEManager.cpp lines 9-11."""

    SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # PC → device (write)
    TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # device → PC (notify)
    DEFAULT_NAME = "HT-LoRa"


# ════════════════════════════════════════════════════════════════════════════
# 2. Config
# ════════════════════════════════════════════════════════════════════════════

@dataclass
class Config:
    device_name_prefix: str  = BLEConstants.DEFAULT_NAME
    device_ip: Optional[str] = None

    # Scan & connect
    scan_timeout: float      = 10.0
    connect_timeout: float   = 15.0

    # Timer intervals (seconds) — four concurrent asyncio tasks
    status_poll_interval: float   = 5.0
    rotation_interval: float      = 8.0
    metrics_dump_interval: float  = 30.0
    watchdog_interval: float      = 12.0
    watchdog_miss_limit: int      = 3

    # BLE write pacing — firmware BLE queue = 4, task polls at 20ms.
    # 150ms gives a 7.5× safety margin vs the 20ms poll rate.
    send_delay: float        = 0.15

    # Mirrors MAX_DYNAMIC_TASKS in ScheduleManager.h
    max_sched_tasks: int     = 5

    # Rotation strategy — one of _ROTATION_STRATEGIES; resolved in main()
    rotation_strategy: str   = "sequential"

    # Session
    log_path: str            = "ble_session.json"
    dry_run: bool            = False
    no_maximize: bool        = False


# ════════════════════════════════════════════════════════════════════════════
# 3. CommandMetric
# ════════════════════════════════════════════════════════════════════════════

@dataclass
class CommandMetric:
    ts: float
    command: str
    group: str
    rotation_index: int
    ble_sent: bool                    = False
    http_sent: bool                   = False
    ble_notify_received: bool         = False
    http_response: Optional[dict]     = None
    latency_ms: Optional[float]       = None
    error: Optional[str]              = None


# ════════════════════════════════════════════════════════════════════════════
# 4. SessionLog
# ════════════════════════════════════════════════════════════════════════════

class SessionLog:
    """Append-only JSON-lines writer. One CommandMetric dict per line."""

    def __init__(self, path: str) -> None:
        self._fh = open(path, "w", buffering=1, encoding="utf-8")

    def record(self, metric: CommandMetric) -> None:
        self._fh.write(json.dumps(asdict(metric)) + "\n")

    def flush_summary(self, stats: dict) -> None:
        self._fh.write(json.dumps({"_type": "session_summary", **stats}) + "\n")
        self._fh.flush()

    def close(self) -> None:
        self._fh.close()


# ════════════════════════════════════════════════════════════════════════════
# 5. ResponseBuffer
# ════════════════════════════════════════════════════════════════════════════

class ResponseBuffer:
    """
    Accumulates BLE TX notifications from the device.

    NOTE: BLEManager.notify() is defined in the firmware but is not currently
    called from any handler. This buffer subscribes to TX notifications so that
    when the firmware is updated to send responses back over BLE, the Python
    side captures them automatically with no changes needed here.
    """

    def __init__(self) -> None:
        self._buf: str = ""
        self._lines: deque[str] = deque(maxlen=50)
        self.total_received: int = 0

    def on_notify(self, sender, data: bytearray) -> None:
        self.total_received += 1
        chunk = data.decode("utf-8", errors="replace")
        self._buf += chunk
        while "\n" in self._buf:
            line, self._buf = self._buf.split("\n", 1)
            line = line.strip()
            if line:
                self._lines.append(line)

    def drain(self) -> list[str]:
        out = list(self._lines)
        self._lines.clear()
        return out


# ════════════════════════════════════════════════════════════════════════════
# 6. BLELink
# ════════════════════════════════════════════════════════════════════════════

class DeviceNotFoundError(Exception):
    pass


class BLELink:
    """
    Manages the bleak BLE connection.

    Scan strategy: filter by name prefix because the firmware advertisement
    uses 0x180F (16-bit Battery Service UUID) as a compact placeholder rather
    than the full 128-bit NUS UUID. The NUS service is verified post-connect
    by inspecting the GATT service table.

    Write strategy: chunk payload at 20 bytes (conservative ATT MTU) and
    append \\n so the firmware BLEManager.onWrite() trim() sees a complete
    token before it enqueues the message.
    """

    def __init__(self, config: Config, response_buf: ResponseBuffer) -> None:
        self._config = config
        self._response_buf = response_buf
        self._client: Optional[BleakClient] = None
        self._device: Optional[BLEDevice] = None
        self._rx_uuid: Optional[str] = None
        self._tx_uuid: Optional[str] = None

    # ── Scan ─────────────────────────────────────────────────────────────────

    async def scan(self) -> BLEDevice:
        console.print(
            f"[cyan]Scanning for '{self._config.device_name_prefix}' "
            f"({self._config.scan_timeout:.0f}s)...[/cyan]"
        )
        devices = await BleakScanner.discover(timeout=self._config.scan_timeout)
        for d in devices:
            if d.name and d.name.startswith(self._config.device_name_prefix):
                console.print(f"[green]Found: {d.name}  ({d.address})[/green]")
                return d
        raise DeviceNotFoundError(
            f"No device with name prefix '{self._config.device_name_prefix}' found "
            f"after {self._config.scan_timeout:.0f}s scan."
        )

    # ── Connect ──────────────────────────────────────────────────────────────

    async def connect(self, device: BLEDevice) -> None:
        self._device = device
        self._client = BleakClient(device, timeout=self._config.connect_timeout)
        await self._client.connect()

        if not self._client.is_connected:
            raise ConnectionError(f"Failed to connect to {device.address}")

        # Verify NUS service is present in the GATT table
        services = self._client.services
        nus_service = services.get_service(BLEConstants.SERVICE_UUID)
        if nus_service is None:
            await self._client.disconnect()
            raise ConnectionError(
                "NUS service UUID not found after connect — wrong device?"
            )

        # Map RX and TX characteristics
        for char in nus_service.characteristics:
            uuid_upper = char.uuid.upper()
            if uuid_upper == BLEConstants.RX_CHAR_UUID.upper():
                self._rx_uuid = char.uuid
            elif uuid_upper == BLEConstants.TX_CHAR_UUID.upper():
                self._tx_uuid = char.uuid

        # Subscribe to TX notifications (future-proof — firmware doesn't use yet)
        if self._tx_uuid:
            await self._client.start_notify(
                self._tx_uuid, self._response_buf.on_notify
            )

        console.print(f"[bold green]Connected to {device.name}[/bold green]")

    # ── Disconnect ───────────────────────────────────────────────────────────

    async def disconnect(self) -> None:
        if self._client and self._client.is_connected:
            if self._tx_uuid:
                try:
                    await self._client.stop_notify(self._tx_uuid)
                except Exception:
                    pass
            await self._client.disconnect()

    # ── Send ─────────────────────────────────────────────────────────────────

    async def send_command(self, cmd: str) -> bool:
        """Write a command string to the RX characteristic. Returns True on success."""
        if self._config.dry_run:
            console.print(f"[dim][DRY-RUN] → {cmd}[/dim]")
            return True

        if not self.is_connected or not self._rx_uuid:
            return False

        payload = (cmd + "\n").encode("utf-8")
        try:
            # Chunk at 20 bytes — conservative ATT_MTU for maximum compatibility
            for i in range(0, len(payload), 20):
                await self._client.write_gatt_char(
                    self._rx_uuid, payload[i : i + 20], response=False
                )
            return True
        except Exception as e:
            console.print(f"[red]BLE write error ({cmd!r}): {e}[/red]")
            return False

    @property
    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    @property
    def device_name(self) -> str:
        return self._device.name if self._device else "Unknown"


# ════════════════════════════════════════════════════════════════════════════
# 7. HttpPoller
# ════════════════════════════════════════════════════════════════════════════

class HttpPoller:
    """
    Optional HTTP interface — active only when Config.device_ip is set.

    Uses the WiFiManager web API:
      GET  /api/status  → full device status JSON (includes last_cmd, mesh, log)
      POST /api/cmd     → body: cmd=<command>
    """

    def __init__(self, ip: str) -> None:
        self._base = f"http://{ip}"
        self._session: Optional[aiohttp.ClientSession] = None

    async def _get_session(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession(
                timeout=aiohttp.ClientTimeout(total=3.0)
            )
        return self._session

    async def get_status(self) -> Optional[dict]:
        if not AIOHTTP:
            return None
        try:
            session = await self._get_session()
            async with session.get(f"{self._base}/api/status") as r:
                return await r.json(content_type=None)
        except Exception:
            return None

    async def post_cmd(self, cmd: str) -> bool:
        if not AIOHTTP:
            return False
        try:
            session = await self._get_session()
            async with session.post(
                f"{self._base}/api/cmd", data={"cmd": cmd}
            ) as r:
                return r.status == 200
        except Exception:
            return False

    async def close(self) -> None:
        if self._session and not self._session.closed:
            await self._session.close()


# ════════════════════════════════════════════════════════════════════════════
# 8. Command Groups
# ════════════════════════════════════════════════════════════════════════════

# Safe relay pins only: 6 (PIN_RELAY_12V_2), 7 (PIN_RELAY_12V_3)
# Pin 14 is shared with LORA_DIO1 — never used here (config.h hardware conflict)
COMMAND_GROUPS: dict[str, list[str]] = {
    "health":       ["STATUS", "RADIO"],
    "mesh":         ["NODES"],
    "gpio_probe":   ["READ LED", "READ 6", "READ 7"],
    "led_exercise": ["LED ON", "LED OFF", "BLINK"],
    "sched_audit":  ["SCHED LIST"],
    "stream_on":    ["STREAM ON"],
    "stream_off":   ["STREAM OFF"],
    "help_probe":   ["HELP"],
}

# Sent once at startup to fill all 5 firmware dynamic task slots.
# ScheduleManager.addDynamicTask() multiplies interval/duration by 1000 (ms).
# Format: SCHED ADD <name> <type> <pin> <interval_sec> [<duration_sec>]
FIRMWARE_MAX_TASKS: list[str] = [
    "SCHED ADD PyHB    TOGGLE LED 3   0",   # slot 0: LED blink (visual confirm)
    "SCHED ADD PyR2Tog TOGGLE 6  30   0",   # slot 1: PIN_RELAY_12V_2 slow toggle
    "SCHED ADD PyR3Pls PULSE  7  45   2",   # slot 2: PIN_RELAY_12V_3 2s pulse/45s
    "SCHED ADD PyR2Pls PULSE  6  60   5",   # slot 3: PIN_RELAY_12V_2 5s pulse/60s
    "SCHED ADD PyR3Tog TOGGLE 7  90   0",   # slot 4: PIN_RELAY_12V_3 slow toggle
]


# ════════════════════════════════════════════════════════════════════════════
# 9. CommandRotator
# ════════════════════════════════════════════════════════════════════════════

class CommandRotator:
    """
    Decides which command group to send next on each rotation timer tick.

    The pick_next_group() method is the user contribution point — implement
    your own rotation strategy there.
    """

    def __init__(self, groups: list[str], config: Config) -> None:
        self.groups  = groups
        self.config  = config
        self.index   = 0
        self.history: list[str] = []

    def pick_next_group(self) -> str:
        """Dispatch to the configured rotation strategy method."""
        strategy = getattr(self.config, "rotation_strategy", "sequential")
        dispatch = {
            "weighted":   self._strategy_weighted,
            "adaptive":   self._strategy_adaptive,
            "random":     self._strategy_random,
            "interleave": self._strategy_interleave,
        }
        return dispatch.get(strategy, self._strategy_sequential)()

    # ── Strategy A: sequential round-robin ───────────────────────────────
    def _strategy_sequential(self) -> str:
        """Visit every group once in order; predictable, even coverage."""
        group = self.groups[self.index % len(self.groups)]
        self.index += 1
        self.history.append(group)
        return group

    # ── Strategy B: weighted (health + mesh run 2× more often) ───────────
    def _strategy_weighted(self) -> str:
        """Draw from a pool where 'health' and 'mesh' appear twice."""
        pool = list(self.groups) + ["health", "health", "mesh"]
        group = random.choice(pool)
        self.history.append(group)
        return group

    # ── Strategy C: adaptive (skip gpio_probe when errors accumulate) ─────
    def _strategy_adaptive(self) -> str:
        """If 3+ of last 5 sends were gpio_probe, skip it this round."""
        recent_gpio = self.history[-5:].count("gpio_probe")
        candidates = [
            g for g in self.groups
            if not (recent_gpio >= 3 and g == "gpio_probe")
        ] or self.groups
        group = candidates[self.index % len(candidates)]
        self.index += 1
        self.history.append(group)
        return group

    # ── Strategy D: random (fuzz-style) ──────────────────────────────────
    def _strategy_random(self) -> str:
        """Uniformly random — maximises unpredictability for fuzz testing."""
        group = random.choice(self.groups)
        self.history.append(group)
        return group

    # ── Strategy E: interleave (health every 2nd slot) ────────────────────
    def _strategy_interleave(self) -> str:
        """Even ticks → 'health'; odd ticks → cycle through remaining groups."""
        if self.index % 2 == 0:
            group = "health"
        else:
            non_health = [g for g in self.groups if g != "health"] or self.groups
            group = non_health[(self.index // 2) % len(non_health)]
        self.index += 1
        self.history.append(group)
        return group


# ════════════════════════════════════════════════════════════════════════════
# 10. FirmwareMaximizer
# ════════════════════════════════════════════════════════════════════════════

class FirmwareMaximizer:
    """
    Sends SCHED ADD commands at session start to fill all 5 firmware dynamic
    task slots (MAX_DYNAMIC_TASKS = 5 from ScheduleManager.h).

    Only safe relay pins 6 and 7 are used. Pin 14 is forbidden — it is shared
    with LORA_DIO1 and enabling both causes a hardware conflict (config.h).
    """

    async def fill_all_slots(self, ble: BLELink, delay: float = 0.5) -> None:
        console.print(
            "[cyan]Maximizing firmware task scheduler "
            f"({len(FIRMWARE_MAX_TASKS)} slots)...[/cyan]"
        )
        for cmd in FIRMWARE_MAX_TASKS:
            sent = await ble.send_command(cmd)
            name = cmd.split()[2]
            status = "✓" if sent else "✗"
            console.print(f"  {status} {name}  ({cmd.strip()})")
            await asyncio.sleep(delay)

        await asyncio.sleep(1.0)
        await ble.send_command("SCHED LIST")   # verify all 5 appear on device
        await asyncio.sleep(0.5)
        await ble.send_command("SCHED SAVE")   # persist to LittleFS

    async def clear_slots(self, ble: BLELink, delay: float = 0.3) -> None:
        console.print("[cyan]Cleaning up firmware scheduler slots...[/cyan]")
        for cmd in FIRMWARE_MAX_TASKS:
            name = cmd.split()[2]
            await ble.send_command(f"SCHED REM {name}")
            await asyncio.sleep(delay)
        await ble.send_command("SCHED SAVE")


# ════════════════════════════════════════════════════════════════════════════
# 11. Orchestrator
# ════════════════════════════════════════════════════════════════════════════

class Orchestrator:
    """
    Wires all components and owns the 4 concurrent asyncio tasks:

      _task_status_poll       5s  — health check via STATUS or HTTP /api/status
      _task_command_rotation  8s  — advance rotator, send all commands in group
      _task_metrics_dump      30s — write snapshot line to session log
      _task_watchdog          12s — detect BLE disconnect, reconnect on miss_limit
    """

    def __init__(self, config: Config) -> None:
        self.config    = config
        self.log       = SessionLog(config.log_path)
        self.resp_buf  = ResponseBuffer()
        self.ble       = BLELink(config, self.resp_buf)
        self.http      = (
            HttpPoller(config.device_ip)
            if config.device_ip and AIOHTTP
            else None
        )
        self.rotator   = CommandRotator(list(COMMAND_GROUPS.keys()), config)
        self.maximizer = FirmwareMaximizer()

        self._running  = False
        self._stats: dict = {
            "sent": 0, "errors": 0, "notified": 0,
            "rotations": 0, "reconnects": 0,
            "start_ts": 0.0,
            "max_tasks_filled": False,
        }
        self._recent: deque[str] = deque(maxlen=10)
        self._last_group = ""

    # ── Main entry ────────────────────────────────────────────────────────────

    async def run(self) -> None:
        self._stats["start_ts"] = time.time()
        self._running = True

        # 1. Scan & connect (skip in dry-run mode)
        if not self.config.dry_run:
            try:
                device = await self.ble.scan()
                await self.ble.connect(device)
            except DeviceNotFoundError as e:
                console.print(f"[red]{e}[/red]")
                return
            except Exception as e:
                console.print(f"[red]Connection error: {e}[/red]")
                return
        else:
            console.print("[dim][DRY-RUN] Skipping BLE scan and connect.[/dim]")

        # 2. Push firmware scheduler to its maximum task count
        if not self.config.no_maximize:
            await self.maximizer.fill_all_slots(self.ble)
            self._stats["max_tasks_filled"] = True

        # 3. Run all 4 timers concurrently
        console.print(
            f"\n[bold cyan]Running 4 async timers — Ctrl-C to stop[/bold cyan]\n"
        )
        try:
            tasks = [
                self._task_status_poll(),
                self._task_command_rotation(),
                self._task_metrics_dump(),
                self._task_watchdog(),
            ]
            if RICH:
                tasks.append(self._task_rich_display())
            await asyncio.gather(*tasks)
        except asyncio.CancelledError:
            pass
        finally:
            await self._shutdown()

    # ── Task 1: Status Poll (5s) ──────────────────────────────────────────────

    async def _task_status_poll(self) -> None:
        while self._running:
            await asyncio.sleep(self.config.status_poll_interval)
            ts = time.time()

            if self.http:
                status = await self.http.get_status()
                metric = CommandMetric(
                    ts=ts, command="GET /api/status", group="__http__",
                    rotation_index=self._stats["rotations"],
                    http_sent=True, http_response=status,
                )
            else:
                sent = await self._send("STATUS", "__poll__")
                metric = CommandMetric(
                    ts=ts, command="STATUS", group="__poll__",
                    rotation_index=self._stats["rotations"],
                    ble_sent=sent,
                )

            self.log.record(metric)

    # ── Task 2: Command Rotation (8s) ─────────────────────────────────────────

    async def _task_command_rotation(self) -> None:
        while self._running:
            await asyncio.sleep(self.config.rotation_interval)

            group_name = self.rotator.pick_next_group()
            commands   = COMMAND_GROUPS.get(group_name, [])
            self._last_group = group_name
            self._stats["rotations"] += 1

            for cmd in commands:
                sent = await self._send(cmd, group_name)

                # Drain any TX notifications that arrived while we worked
                notifs = self.resp_buf.drain()
                notify_hit = len(notifs) > 0
                self._stats["notified"] += len(notifs)

                self.log.record(CommandMetric(
                    ts=time.time(), command=cmd, group=group_name,
                    rotation_index=self._stats["rotations"],
                    ble_sent=sent,
                    ble_notify_received=notify_hit,
                ))
                await asyncio.sleep(self.config.send_delay)

    # ── Task 3: Metrics Dump (30s) ────────────────────────────────────────────

    async def _task_metrics_dump(self) -> None:
        while self._running:
            await asyncio.sleep(self.config.metrics_dump_interval)
            uptime = time.time() - self._stats["start_ts"]
            snap = {
                "commands_sent":   self._stats["sent"],
                "ble_errors":      self._stats["errors"],
                "notify_received": self._stats["notified"],
                "groups_cycled":   self._stats["rotations"],
                "uptime_s":        round(uptime, 1),
                "device":          self.ble.device_name,
                "max_tasks_filled": self._stats["max_tasks_filled"],
            }
            self.log.flush_summary(snap)
            console.print(
                f"[dim]» Snapshot: {snap['commands_sent']} sent, "
                f"{snap['ble_errors']} errors, {uptime:.0f}s uptime[/dim]"
            )

    # ── Task 4: Watchdog / Auto-Reconnect (12s) ───────────────────────────────

    async def _task_watchdog(self) -> None:
        miss_count = 0
        while self._running:
            await asyncio.sleep(self.config.watchdog_interval)
            if self.config.dry_run:
                continue
            if not self.ble.is_connected:
                miss_count += 1
                console.print(
                    f"[yellow]Watchdog: miss {miss_count}"
                    f"/{self.config.watchdog_miss_limit}[/yellow]"
                )
                if miss_count >= self.config.watchdog_miss_limit:
                    miss_count = 0
                    await self._reconnect()
            else:
                miss_count = 0

    async def _reconnect(self) -> None:
        self._stats["reconnects"] += 1
        console.print("[yellow]Watchdog: attempting reconnect...[/yellow]")
        try:
            device = await self.ble.scan()
            await self.ble.connect(device)
        except Exception as e:
            console.print(f"[red]Reconnect failed: {e}[/red]")

    # ── Task 5: Rich live display refresh (rich-only, 0.5s) ──────────────────

    async def _task_rich_display(self) -> None:
        """Runs only when rich is installed. Refreshes the terminal UI at 2 Hz."""
        layout = Layout()
        layout.split_column(
            Layout(name="header", size=3),
            Layout(name="body"),
            Layout(name="footer", size=3),
        )
        layout["body"].split_row(
            Layout(name="metrics"),
            Layout(name="log"),
        )

        with Live(layout, refresh_per_second=2, console=console):
            while self._running:
                uptime = time.time() - self._stats["start_ts"]
                conn = (
                    "[bold green]CONNECTED[/bold green]"
                    if self.ble.is_connected
                    else "[bold red]DISCONNECTED[/bold red]"
                )

                layout["header"].update(Panel(
                    f"{conn}  |  Device: [cyan]{self.ble.device_name}[/cyan]  |  "
                    f"Uptime: {uptime:.0f}s  |  Log: {self.config.log_path}",
                    title="[bold]LoRaLink BLE Instrument[/bold]",
                ))

                t = Table(box=None, show_header=True, header_style="bold cyan")
                t.add_column("Metric", style="cyan")
                t.add_column("Value", justify="right")
                for label, key in [
                    ("Commands Sent", "sent"),
                    ("BLE Errors",    "errors"),
                    ("TX Notified",   "notified"),
                    ("Rotations",     "rotations"),
                    ("Reconnects",    "reconnects"),
                ]:
                    t.add_row(label, str(self._stats[key]))
                layout["metrics"].update(Panel(t, title="Metrics"))

                log_text = "\n".join(self._recent) if self._recent else "[dim]waiting...[/dim]"
                layout["log"].update(Panel(log_text, title="Command Log"))

                layout["footer"].update(Panel(
                    f"Last group: [bold yellow]{self._last_group}[/bold yellow]  |  "
                    f"TX notifications: {self.resp_buf.total_received}  |  "
                    f"Max tasks filled: {self._stats['max_tasks_filled']}",
                    title="Rotation",
                ))

                await asyncio.sleep(0.5)

    # ── Helpers ───────────────────────────────────────────────────────────────

    async def _send(self, cmd: str, group: str) -> bool:
        sent = await self.ble.send_command(cmd)
        self._stats["sent"] += 1
        if not sent:
            self._stats["errors"] += 1
        entry = f"[{time.strftime('%H:%M:%S')}] [{group}] {cmd}"
        self._recent.append(entry)
        if not RICH:
            print(entry)
        return sent

    # ── Shutdown ──────────────────────────────────────────────────────────────

    async def _shutdown(self) -> None:
        self._running = False

        # Clean up firmware scheduler slots before disconnecting
        if not self.config.no_maximize and (
            self.config.dry_run or self.ble.is_connected
        ):
            await self.maximizer.clear_slots(self.ble)

        # Final session summary
        uptime = time.time() - self._stats["start_ts"]
        self.log.flush_summary({
            "commands_sent":    self._stats["sent"],
            "ble_errors":       self._stats["errors"],
            "notify_received":  self._stats["notified"],
            "groups_cycled":    self._stats["rotations"],
            "reconnects":       self._stats["reconnects"],
            "uptime_s":         round(uptime, 1),
            "device":           self.ble.device_name,
            "max_tasks_filled": self._stats["max_tasks_filled"],
        })
        self.log.close()

        if self.http:
            await self.http.close()

        await self.ble.disconnect()

        console.print(
            f"\n[bold]Session complete:[/bold] "
            f"{self._stats['sent']} commands sent  |  "
            f"{self._stats['errors']} errors  |  "
            f"{uptime:.0f}s uptime\n"
            f"Log written to: [cyan]{self.config.log_path}[/cyan]"
        )


# ════════════════════════════════════════════════════════════════════════════
# CLI entry point
# ════════════════════════════════════════════════════════════════════════════

def _resolve_rotation_strategy(cli_rotation: Optional[str]) -> str:
    """
    Determine which rotation strategy to use and persist the choice.

    Priority: CLI --rotation flag > saved pref > interactive menu.
    Saves the resolved choice to _PREFS_FILE so the next run skips the menu.
    """
    prefs: dict = {}
    if _PREFS_FILE.exists():
        try:
            prefs = json.loads(_PREFS_FILE.read_text())
        except Exception:
            pass

    if cli_rotation and cli_rotation in _ROTATION_STRATEGIES:
        strategy = cli_rotation
        console.print(f"  Rotation      : [cyan]{strategy}[/cyan] (--rotation flag)")
    elif prefs.get("rotation_strategy") in _ROTATION_STRATEGIES:
        strategy = prefs["rotation_strategy"]
        console.print(f"  Rotation      : [cyan]{strategy}[/cyan] (saved preference)")
    else:
        # Interactive menu — runs synchronously before asyncio loop starts
        labels = {
            "sequential": "Sequential round-robin (even coverage, predictable)",
            "weighted":   "Weighted  (health & mesh run 2× more often)",
            "adaptive":   "Adaptive  (skips gpio_probe when errors accumulate)",
            "random":     "Random    (fuzz-style, maximum unpredictability)",
            "interleave": "Interleave (health every 2nd slot, others fill odd slots)",
        }
        console.print("\n[bold cyan]Select BLE Command Rotation Strategy[/bold cyan]")
        for i, key in enumerate(_ROTATION_STRATEGIES, 1):
            console.print(f"  [bold]{i}[/bold]. {labels[key]}")
        strategy = _ROTATION_STRATEGIES[0]
        try:
            raw = input(f"Enter choice [1-{len(_ROTATION_STRATEGIES)}] (default: 1 sequential): ").strip()
            idx = int(raw) - 1
            if 0 <= idx < len(_ROTATION_STRATEGIES):
                strategy = _ROTATION_STRATEGIES[idx]
        except (ValueError, EOFError, KeyboardInterrupt):
            pass
        console.print(f"  Rotation      : [cyan]{strategy}[/cyan]")

    # Persist for next run
    _PREFS_FILE.write_text(json.dumps({"rotation_strategy": strategy}, indent=2))
    return strategy


def _build_config(args: argparse.Namespace, rotation_strategy: str) -> Config:
    return Config(
        device_name_prefix = args.device,
        device_ip          = args.ip,
        rotation_interval  = args.interval,
        rotation_strategy  = rotation_strategy,
        log_path           = args.log,
        dry_run            = args.dry_run,
        no_maximize        = args.no_maximize,
    )


async def _main(config: Config) -> None:
    orch = Orchestrator(config)
    try:
        await orch.run()
    except KeyboardInterrupt:
        pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description="LoRaLink-AnyToAny BLE Instrumentation Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--device", default="HT-LoRa",
        metavar="PREFIX",
        help="BLE device name prefix to scan for  (default: HT-LoRa)",
    )
    parser.add_argument(
        "--ip", default=None,
        metavar="ADDRESS",
        help="Device IP for HTTP /api/status polling  (optional)",
    )
    parser.add_argument(
        "--interval", type=float, default=8.0,
        metavar="SECS",
        help="Command rotation interval in seconds  (default: 8)",
    )
    parser.add_argument(
        "--log", default="ble_session.json",
        metavar="PATH",
        help="Session log output path  (default: ble_session.json)",
    )
    parser.add_argument(
        "--no-maximize", action="store_true",
        help="Skip SCHED ADD burst at startup",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Simulate BLE writes without connecting  (for testing)",
    )
    parser.add_argument(
        "--rotation",
        choices=_ROTATION_STRATEGIES,
        default=None,
        metavar="STRATEGY",
        help=(
            f"Command rotation strategy: {', '.join(_ROTATION_STRATEGIES)}  "
            "(default: prompt or saved preference)"
        ),
    )

    args = parser.parse_args()

    console.print("[bold cyan]LoRaLink BLE Instrumentation Framework[/bold cyan]")
    console.print(f"  Device prefix : {args.device}")
    console.print(f"  HTTP polling  : {args.ip or 'disabled'}")
    console.print(f"  Rotate every  : {args.interval}s")
    console.print(f"  Log path      : {args.log}")
    console.print(f"  Maximize      : {'disabled' if args.no_maximize else f'enabled ({len(FIRMWARE_MAX_TASKS)} SCHED slots)'}")
    console.print(f"  Dry run       : {args.dry_run}")

    # Resolve rotation strategy (menu fires here if no flag / no saved pref)
    rotation = _resolve_rotation_strategy(args.rotation)
    config = _build_config(args, rotation)
    console.print()

    asyncio.run(_main(config))


if __name__ == "__main__":
    main()
