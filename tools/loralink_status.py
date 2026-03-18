#!/usr/bin/env python3
"""
loralink_status.py — LoRaLink Fleet Status Tool

Query status from LoRaLink devices via WiFi (HTTP), BLE, or Serial (COM port).
Auto-detects address type from format.

Usage:
    python tools\loralink_status.py 172.16.0.26 172.16.0.27
    python tools\loralink_status.py --range 172.16.0.26-30
    python tools\loralink_status.py COM7 COM18
    python tools\loralink_status.py "HT-LoRa" aa:bb:cc:dd:ee:ff
    python tools\loralink_status.py --range 172.16.0.26-30 --json
    python tools\loralink_status.py --scan-ble
    python tools\loralink_status.py --range 172.16.0.26-30 --watch 5

Address types (auto-detected):
    192.168.x.x / 172.x.x.x  →  WiFi  (HTTP GET /api/status)
    COMx / /dev/ttyUSBx       →  Serial (pyserial, send STATUS\\n)
    xx:xx:xx:xx:xx:xx         →  BLE MAC
    anything else              →  BLE device name prefix

Install dependencies:
    pip install -r tools/requirements.txt
"""
from __future__ import annotations

import argparse
import asyncio
import json
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

# ── Optional: rich ───────────────────────────────────────────────────────────
try:
    from rich.console import Console
    from rich.table import Table
    from rich.text import Text
    from rich.live import Live
    from rich import box
    RICH = True
    console = Console(highlight=False)
except ImportError:
    RICH = False
    class _FallbackConsole:
        def print(self, *a, **kw): print(*a)
        def log(self, *a, **kw): print(*a)
    console = _FallbackConsole()  # type: ignore

# ── Optional: aiohttp (WiFi) ─────────────────────────────────────────────────
try:
    import aiohttp
    AIOHTTP = True
except ImportError:
    try:
        import urllib.request
        AIOHTTP = False
    except ImportError:
        AIOHTTP = False

# ── Optional: bleak (BLE) ────────────────────────────────────────────────────
try:
    from bleak import BleakClient, BleakScanner
    BLEAK = True
except ImportError:
    BLEAK = False

# ── Optional: pyserial ───────────────────────────────────────────────────────
try:
    import serial
    import serial.tools.list_ports
    PYSERIAL = True
except ImportError:
    PYSERIAL = False

# ── BLE NUS UUIDs (Nordic UART Service) ──────────────────────────────────────
NUS_SERVICE_UUID  = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify (device→PC)
NUS_RX_CHAR_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write  (PC→device)


# ─────────────────────────────────────────────────────────────────────────────
# Data model
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class DeviceStatus:
    address: str
    transport: str          # "wifi" | "ble" | "serial"
    ok: bool = False
    error: str = ""
    raw: dict = field(default_factory=dict)

    # Parsed fields
    id: str = "—"
    version: str = "—"
    uptime: str = "—"
    heap: str = "—"
    bat: str = "—"
    lora_state: str = "—"
    lora_tx: int = 0
    lora_rx: int = 0
    lora_drop: int = 0
    power_mode: str = "—"
    wifi: bool = False
    ble: bool = False
    espnow: bool = False
    last_cmd: str = "—"
    latency_ms: int = 0

    def parse(self, data: dict) -> "DeviceStatus":
        self.raw = data
        self.ok = True
        self.id         = data.get("id", "—")
        self.version    = data.get("version", "—")
        self.uptime     = data.get("uptime", "—")
        heap = data.get("heap", 0)
        self.heap       = f"{heap//1024}KB" if heap else "—"
        bat = data.get("bat", 0.0)
        self.bat        = f"{bat:.2f}V" if bat else "—"
        self.lora_state = data.get("radio_state", "—")
        self.lora_tx    = data.get("lora_tx", 0)
        self.lora_rx    = data.get("lora_rx", 0)
        self.lora_drop  = data.get("lora_drop", 0)
        self.power_mode = data.get("power_mode", "—")
        self.wifi       = data.get("wifi", False)
        self.ble        = data.get("ble", False)
        self.espnow     = data.get("espnow", False)
        self.last_cmd   = data.get("last_cmd", "—")[:32]
        return self


# ─────────────────────────────────────────────────────────────────────────────
# Address detection
# ─────────────────────────────────────────────────────────────────────────────

def detect_type(addr: str) -> str:
    """Return 'wifi', 'serial', 'ble_mac', or 'ble_name'."""
    # IP address pattern
    if re.match(r"^\d{1,3}(\.\d{1,3}){3}$", addr):
        return "wifi"
    # COM port (Windows or Linux)
    if re.match(r"^(COM\d+|/dev/tty(USB|ACM|S)\d+)$", addr, re.IGNORECASE):
        return "serial"
    # BLE MAC address
    if re.match(r"^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$", addr):
        return "ble_mac"
    # Default: BLE device name prefix
    return "ble_name"


def expand_range(range_str: str) -> list[str]:
    """Expand '172.16.0.26-30' → ['172.16.0.26', ..., '172.16.0.30']."""
    m = re.match(r"^(\d{1,3}\.\d{1,3}\.\d{1,3}\.)(\d+)-(\d+)$", range_str)
    if m:
        prefix, start, end = m.group(1), int(m.group(2)), int(m.group(3))
        return [f"{prefix}{i}" for i in range(start, end + 1)]
    return [range_str]


# ─────────────────────────────────────────────────────────────────────────────
# WiFi query
# ─────────────────────────────────────────────────────────────────────────────

async def query_wifi(addr: str, timeout: float) -> DeviceStatus:
    status = DeviceStatus(address=addr, transport="wifi")
    url = f"http://{addr}/api/status"
    t0 = time.monotonic()
    try:
        if AIOHTTP:
            async with aiohttp.ClientSession() as session:
                async with session.get(url, timeout=aiohttp.ClientTimeout(total=timeout)) as resp:
                    if resp.status == 200:
                        data = await resp.json(content_type=None)
                        status.latency_ms = int((time.monotonic() - t0) * 1000)
                        status.parse(data)
                    else:
                        status.error = f"HTTP {resp.status}"
        else:
            # Fallback: urllib (sync, run in executor)
            import urllib.request
            loop = asyncio.get_event_loop()
            def _fetch():
                with urllib.request.urlopen(url, timeout=timeout) as r:
                    return json.loads(r.read().decode())
            data = await loop.run_in_executor(None, _fetch)
            status.latency_ms = int((time.monotonic() - t0) * 1000)
            status.parse(data)
    except asyncio.TimeoutError:
        status.error = "timeout"
    except Exception as e:
        status.error = type(e).__name__
    return status


# ─────────────────────────────────────────────────────────────────────────────
# Serial query
# ─────────────────────────────────────────────────────────────────────────────

def _parse_serial_text(line: str) -> Optional[dict]:
    """Parse v0.1.0 plain-text STATUS response into a dict.

    Format: ID: Peer1 (HW: [C8E658]) VER: v0.1.0 IP: 172.16.0.27 BAT: 0.00V [NORMAL] LoRa: -120dBm EN:ON
    """
    d: dict = {}
    m = re.search(r"ID:\s*(\S+)", line)
    if m: d["id"] = m.group(1)
    m = re.search(r"HW:\s*\[([^\]]+)\]", line)
    if m: d["hw"] = m.group(1)
    m = re.search(r"VER:\s*(\S+)", line)
    if m: d["version"] = m.group(1)
    m = re.search(r"IP:\s*(\S+)", line)
    if m: d["ip"] = m.group(1)
    m = re.search(r"BAT:\s*([\d.]+)V", line)
    if m: d["bat"] = float(m.group(1))
    m = re.search(r"\[(\w+)\]", line)
    if m: d["power_mode"] = m.group(1)
    m = re.search(r"LoRa:\s*([-\d]+)dBm", line)
    if m: d["rssi"] = int(m.group(1))
    m = re.search(r"EN:(ON|OFF)", line)
    if m: d["espnow"] = m.group(1) == "ON"
    if d.get("id"):
        d["radio_state"] = "rx"
        d["wifi"] = d.get("ip", "DISCONNECTED") not in ("DISCONNECTED", "")
        d["ble"] = True
        return d
    return None


async def query_serial(port: str, timeout: float) -> DeviceStatus:
    status = DeviceStatus(address=port, transport="serial")
    if not PYSERIAL:
        status.error = "pyserial not installed"
        return status
    t0 = time.monotonic()
    try:
        loop = asyncio.get_event_loop()
        def _query():
            with serial.Serial(port, 115200, timeout=timeout) as ser:
                ser.flushInput()
                ser.write(b"STATUS\n")
                deadline = time.monotonic() + timeout
                buf = b""
                while time.monotonic() < deadline:
                    chunk = ser.read(ser.in_waiting or 1)
                    buf += chunk
                    text = buf.decode(errors="ignore")
                    # Try JSON first (v2 firmware)
                    try:
                        start = text.index("{")
                        end = text.rindex("}") + 1
                        return json.loads(text[start:end]), "json"
                    except (ValueError, json.JSONDecodeError):
                        pass
                    # Try plain text (v0.1.0 firmware)
                    for line in text.splitlines():
                        if "ID:" in line and "VER:" in line:
                            return line, "text"
                return None, None
        result, fmt = await loop.run_in_executor(None, _query)
        status.latency_ms = int((time.monotonic() - t0) * 1000)
        if result and fmt == "json":
            status.parse(result)
        elif result and fmt == "text":
            data = _parse_serial_text(result)
            if data:
                status.parse(data)
            else:
                status.error = "unrecognized serial response"
        else:
            status.error = "no response"
    except serial.SerialException as e:
        status.error = str(e)[:40]
    except Exception as e:
        status.error = type(e).__name__
    return status


# ─────────────────────────────────────────────────────────────────────────────
# BLE query
# ─────────────────────────────────────────────────────────────────────────────

async def query_ble(addr: str, addr_type: str, timeout: float) -> DeviceStatus:
    status = DeviceStatus(address=addr, transport="ble")
    if not BLEAK:
        status.error = "bleak not installed"
        return status
    t0 = time.monotonic()
    try:
        # If name, scan first
        target = addr
        if addr_type == "ble_name":
            devices = await BleakScanner.discover(timeout=min(timeout, 5.0))
            match = next((d for d in devices if d.name and d.name.startswith(addr)), None)
            if not match:
                status.error = f"BLE scan: '{addr}' not found"
                return status
            target = match.address

        received: list[str] = []

        async with BleakClient(target, timeout=timeout) as client:
            async def _handler(_char, data: bytearray):
                received.append(data.decode(errors="ignore"))

            await client.start_notify(NUS_TX_CHAR_UUID, _handler)
            await client.write_gatt_char(NUS_RX_CHAR_UUID, b"STATUS\n", response=False)
            await asyncio.sleep(2.0)
            await client.stop_notify(NUS_TX_CHAR_UUID)

        buf = "".join(received)
        try:
            start = buf.index("{")
            end = buf.rindex("}") + 1
            data = json.loads(buf[start:end])
            status.latency_ms = int((time.monotonic() - t0) * 1000)
            status.parse(data)
        except (ValueError, json.JSONDecodeError):
            status.error = "no JSON in BLE response"
    except Exception as e:
        status.error = str(e)[:40]
    return status


# ─────────────────────────────────────────────────────────────────────────────
# BLE scan (--scan-ble)
# ─────────────────────────────────────────────────────────────────────────────

async def scan_ble(timeout: float = 8.0) -> list[str]:
    if not BLEAK:
        console.print("ERROR: bleak not installed -- pip install bleak")
        return []
    console.print(f"Scanning for BLE devices ({timeout:.0f}s)...")
    devices = await BleakScanner.discover(timeout=timeout)
    addrs = []
    for d in sorted(devices, key=lambda x: x.name or "~"):
        name = d.name or "(no name)"
        marker = " <-- LoRaLink" if d.name and ("Peer" in d.name or "HT-" in d.name or "lora" in d.name.lower()) else ""
        print(f"  {d.address}  {name}{marker}")
        addrs.append(d.address)
    return addrs


# ─────────────────────────────────────────────────────────────────────────────
# Dispatch
# ─────────────────────────────────────────────────────────────────────────────

async def query_device(addr: str, timeout: float) -> DeviceStatus:
    t = detect_type(addr)
    if t == "wifi":
        return await query_wifi(addr, timeout)
    elif t == "serial":
        return await query_serial(addr, timeout)
    elif t in ("ble_mac", "ble_name"):
        return await query_ble(addr, t, timeout)
    else:
        s = DeviceStatus(address=addr, transport="unknown")
        s.error = "unrecognized address format"
        return s


async def query_all(addresses: list[str], timeout: float) -> list[DeviceStatus]:
    # WiFi in parallel, BLE/Serial sequentially (hardware constraints)
    wifi_addrs = [a for a in addresses if detect_type(a) == "wifi"]
    other_addrs = [a for a in addresses if detect_type(a) != "wifi"]

    wifi_tasks = [query_wifi(a, timeout) for a in wifi_addrs]
    wifi_results = await asyncio.gather(*wifi_tasks)

    other_results = []
    for a in other_addrs:
        r = await query_device(a, timeout)
        other_results.append(r)

    # Preserve original order
    result_map = {r.address: r for r in list(wifi_results) + other_results}
    return [result_map[a] for a in addresses]


# ─────────────────────────────────────────────────────────────────────────────
# Display
# ─────────────────────────────────────────────────────────────────────────────

def _bool_badge(v: bool) -> str:
    return "Y" if v else "N"


def print_table(results: list[DeviceStatus], show_time: bool = True):
    timestamp = time.strftime("%H:%M:%S")

    if RICH:
        t = Table(
            title=f"LoRaLink Fleet Status  [{timestamp}]",
            box=box.SIMPLE_HEAVY,
            show_lines=False,
            header_style="bold cyan",
            expand=False,
            min_width=100,
        )
        t.add_column("Address",    style="dim",        no_wrap=True, min_width=13)
        t.add_column("Via",        style="dim",        no_wrap=True, min_width=5)
        t.add_column("ID",         style="bold white", no_wrap=True, min_width=10)
        t.add_column("Ver",        no_wrap=True,       min_width=6)
        t.add_column("Uptime",     no_wrap=True,       min_width=10)
        t.add_column("Heap",       no_wrap=True,       min_width=5)
        t.add_column("LoRa",       no_wrap=True,       min_width=4)
        t.add_column("TX/RX/Drop", no_wrap=True,       min_width=10)
        t.add_column("W·B·E",      no_wrap=True,       min_width=5)
        t.add_column("Power",      no_wrap=True,       min_width=7)
        t.add_column("Last Cmd",                       min_width=20)
        t.add_column("ms",         justify="right",    no_wrap=True, min_width=4)

        for r in results:
            if not r.ok:
                t.add_row(
                    r.address,
                    r.transport,
                    Text("OFFLINE", style="red bold"),
                    "—", "—", "—", "—", "—", "—", "—",
                    Text(r.error, style="red"),
                    "—",
                )
            else:
                lora_col = Text(r.lora_state.upper(), style="green" if r.lora_state == "rx" else "yellow")
                txrx = f"{r.lora_tx}/{r.lora_rx}/{r.lora_drop}"
                wbe = f"{_bool_badge(r.wifi)}/{_bool_badge(r.ble)}/{_bool_badge(r.espnow)}"
                t.add_row(
                    r.address,
                    r.transport,
                    Text(r.id, style="bold cyan"),
                    Text(r.version, style="green" if r.version >= "v0.2" else "yellow"),
                    r.uptime,
                    r.heap,
                    lora_col,
                    txrx,
                    wbe,
                    r.power_mode,
                    r.last_cmd,
                    str(r.latency_ms),
                )
        console.print(t)
        online = sum(1 for r in results if r.ok)
        console.print(f"  {online}/{len(results)} online\n", style="dim")
    else:
        # Plain text fallback
        print(f"\n{'='*90}")
        print(f"  LoRaLink Fleet Status  [{timestamp}]")
        print(f"{'='*90}")
        fmt = "{:<18} {:<7} {:<12} {:<8} {:<12} {:<6} {:<5} {:<10} {:<8} {}"
        print(fmt.format("Address","Trans","ID","Version","Uptime","Heap","LoRa","TX/RX/Drop","Power","Last Cmd"))
        print("-"*90)
        for r in results:
            if not r.ok:
                print(f"{r.address:<18} {r.transport:<7} OFFLINE  ({r.error})")
            else:
                txrx = f"{r.lora_tx}/{r.lora_rx}/{r.lora_drop}"
                print(fmt.format(r.address, r.transport, r.id, r.version, r.uptime,
                                 r.heap, r.lora_state, txrx, r.power_mode, r.last_cmd))
        online = sum(1 for r in results if r.ok)
        print(f"\n  {online}/{len(results)} online\n")


def print_json(results: list[DeviceStatus]):
    out = []
    for r in results:
        if r.ok:
            out.append(r.raw)
        else:
            out.append({"address": r.address, "transport": r.transport,
                        "ok": False, "error": r.error})
    print(json.dumps(out, indent=2))


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="LoRaLink fleet status tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("addresses", nargs="*",
                   help="IP address, COMx, BLE MAC, or BLE name")
    p.add_argument("--range", "-r", dest="ip_range",
                   help="IP range e.g. 172.16.0.26-30")
    p.add_argument("--scan-ble", action="store_true",
                   help="Scan and list nearby BLE devices")
    p.add_argument("--json", "-j", action="store_true",
                   help="Output raw JSON instead of table")
    p.add_argument("--timeout", "-t", type=float, default=6.0,
                   help="Per-device timeout in seconds (default: 6)")
    p.add_argument("--watch", "-w", type=float, default=0,
                   metavar="INTERVAL",
                   help="Repeat every N seconds (e.g. --watch 5)")
    p.add_argument("--list-serial", action="store_true",
                   help="List available COM/serial ports and exit")
    return p


async def main_async(args):
    # List serial ports
    if args.list_serial:
        if not PYSERIAL:
            console.print("ERROR: pyserial not installed")
            return
        ports = serial.tools.list_ports.comports()
        if ports:
            for p in ports:
                console.print(f"  {p.device:<10}  {p.description}")
        else:
            console.print("No serial ports found")
        return

    # BLE scan only
    if args.scan_ble:
        await scan_ble(timeout=args.timeout * 2)
        return

    # Collect addresses
    addresses: list[str] = list(args.addresses)
    if args.ip_range:
        addresses += expand_range(args.ip_range)

    if not addresses:
        console.print("No addresses specified. Use --help for usage.")
        return

    # Deduplicate preserving order
    seen = set()
    addresses = [a for a in addresses if not (a in seen or seen.add(a))]

    if args.watch > 0:
        # Repeating watch mode
        while True:
            results = await query_all(addresses, args.timeout)
            if RICH:
                console.clear()
            if args.json:
                print_json(results)
            else:
                print_table(results)
            await asyncio.sleep(args.watch)
    else:
        results = await query_all(addresses, args.timeout)
        if args.json:
            print_json(results)
        else:
            print_table(results)


def main():
    parser = build_parser()
    args = parser.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
