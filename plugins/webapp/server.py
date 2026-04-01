#!/usr/bin/env python3
"""
server.py — Magic PC Control Webapp Backend

Serves the 4-panel browser dashboard at http://localhost:8000.
Talks to the ESP32 over HTTP (primary) and BLE (fallback).

Usage:
    # HTTP + BLE hybrid  (device on WiFi)
    python tools/webapp/server.py --device HT-LoRa --ip 192.168.1.50

    # BLE only  (no WiFi configured on device)
    python tools/webapp/server.py --device HT-LoRa

    # Then open: http://localhost:8000
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import copy
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, Set, TYPE_CHECKING, Any

if TYPE_CHECKING:
    from zeroconf import ServiceBrowser, ServiceInfo, Zeroconf, ServiceListener  # type: ignore
    import serial  # type: ignore
    import aiohttp  # type: ignore
    # Note: BLE imports removed from here to avoid redefinition conflicts with local assignments

import threading
import uuid
import aiofiles
import socket

# zeroconf — for mDNS discovery
try:
    from zeroconf import ServiceBrowser, ServiceInfo, Zeroconf, ServiceListener  # type: ignore
    ZEROCONF = True
except ImportError:
    ZEROCONF = False
    class ServiceListener:  # type: ignore
        def add_service(self, *args, **kwargs): pass
        def update_service(self, *args, **kwargs): pass
        def remove_service(self, *args, **kwargs): pass
    class Zeroconf:  # type: ignore
        def __init__(self, *args, **kwargs): pass
        def get_service_info(self, *args, **kwargs): return None
        def close(self): pass
    class ServiceBrowser:  # type: ignore
        def __init__(self, *args, **kwargs): pass
        def cancel(self): pass
    class ServiceInfo:  # type: ignore
        def __init__(self, *args, **kwargs):
            self.properties = {}
            self.addresses = [b'\x00\x00\x00\x00']
    print("WARNING: zeroconf not installed — mDNS discovery disabled")
    
# Twilio — for SMS integration
try:
    from twilio.twiml.messaging_response import MessagingResponse
    TWILIO = True
except ImportError:
    TWILIO = False
    class MessagingResponse:
        def message(self, *args, **kwargs): pass
    TWILIO_WARNING = "WARNING: twilio not installed — SMS webhook disabled"

# pyserial — optional, degrades gracefully
try:
    import serial
    import serial.tools.list_ports
    PYSERIAL = True
except ImportError:
    PYSERIAL = False
    class DummySerial:
        class Serial:
            def __init__(self, *args, **kwargs):
                self.is_open = False
            def close(self): pass
            def write(self, data): return len(data)
            def read(self, size=1): return b""
            def flush(self): pass
        class tools:
            class list_ports:
                @staticmethod
                def comports(): return []
    serial = DummySerial
    print("WARNING: pyserial not installed — Serial transport disabled")

# ── FastAPI / WebSocket ──────────────────────────────────────────────────────
from contextlib import asynccontextmanager
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Form
from fastapi.responses import (
    FileResponse,
    HTMLResponse,
    JSONResponse,
    PlainTextResponse,
    Response,
)
from fastapi.staticfiles import StaticFiles
import uvicorn

# ── aiohttp for device HTTP ──────────────────────────────────────────────────
try:
    import aiohttp  # type: ignore
    AIOHTTP = True
except ImportError:
    AIOHTTP = False
    class DummyAiohttp:
        class ClientSession:
            def __init__(self, *args, **kwargs):
                self.closed = True
            async def __aenter__(self): return self
            async def __aexit__(self, *args): pass
            async def close(self): pass
            
            class _RequestContextManager:
                def __init__(self):
                    self.status = 200
                    self.content_type = "application/json"
                async def __aenter__(self): return self
                async def __aexit__(self, *args): pass
                async def json(self, *args, **kwargs): return {}
                async def text(self, *args, **kwargs): return ""
                async def read(self): return b""
            
            def get(self, *args, **kwargs): return self._RequestContextManager()
            def post(self, *args, **kwargs): return self._RequestContextManager()
            
        class ClientTimeout:
            def __init__(self, *args, **kwargs): pass
    aiohttp = DummyAiohttp  # type: ignore

# ── BLE stack — import from sibling ble_instrument.py ────────────────────────
_tools_dir = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_tools_dir))
try:
    from ble_instrument import (  # type: ignore
        BLELink,
        BLEConstants,
        Config as BLEConfig,
        ResponseBuffer,
    )
    from bleak import BleakScanner  # type: ignore
    BLEAK = True
except ImportError:
    BLEAK = False
    class BleakScanner:  # type: ignore
        @staticmethod
        async def discover(timeout: float = 10.0, **kwargs: Any) -> list: return []
    class BLELink:  # type: ignore
        def __init__(self, *args, **kwargs):
            self.is_connected = False
            self.device_name = "Dummy"
        async def connect(self, dev: Any) -> None: pass
        async def send_command(self, *args, **kwargs): return False
    class BLEConstants: pass  # type: ignore
    class BLEConfig:  # type: ignore
        def __init__(self, **kwargs: Any): pass
    class ResponseBuffer: pass  # type: ignore
    print("WARNING: bleak not available — BLE transport disabled")

try:
    from simulator import SimulatorBridge
    SIMULATOR_AVAILABLE = True
except ImportError:
    SIMULATOR_AVAILABLE = False

# ── Daemon client — communicates with Magic PC Daemon on :8001 ─────────────
try:
    from tools.webapp.daemon_client import DaemonClient, BaseDeviceClient
    DAEMON_CLIENT_AVAILABLE = True
except ImportError:
    try:
        # Fallback for when running directly from tools/webapp/
        from daemon_client import DaemonClient, BaseDeviceClient  # type: ignore
        DAEMON_CLIENT_AVAILABLE = True
    except ImportError:
        DAEMON_CLIENT_AVAILABLE = False
        class BaseDeviceClient:  # type: ignore
            """Stub ABC when daemon_client.py is not importable."""
        class DaemonClient:  # type: ignore
            """Stub when daemon_client.py is not importable."""
            def __init__(self, *args, **kwargs): pass
            async def connect(self): pass
            async def disconnect(self): pass
            async def health(self) -> bool: return False
            async def list_nodes(self) -> list: return []
            async def send_command(self, *args, **kwargs) -> bool: return False
            async def get_messages(self, *args, **kwargs) -> list: return []
        print("WARNING: daemon_client not available — daemon integration disabled")


STATIC_DIR = Path(__file__).parent / "static"
SETTINGS_FILE = Path(__file__).parent / ".settings.json"

NODES_FILE = Path(__file__).parent / ".nodes.json"
SEQUENCES_FILE = Path(__file__).parent / ".sequences.json"
CONFIGS_DIR = Path(__file__).parent / "configs"

_TRANSPORT_STRATEGIES = [
    "http_first",  # HTTP when reachable; BLE after 3 consecutive failures
    "ble_only",  # Always BLE; ignores HTTP entirely
    "readwrite_split",  # STATUS/MESH → HTTP; GPIO/RELAY/SCHED → BLE
    "immediate_fallback",  # HTTP only when zero failures on record; else BLE
    "roundrobin",  # Alternate HTTP/BLE on every command
    "serial_only",  # Always Serial; ignores HTTP and BLE entirely
]

# ════════════════════════════════════════════════════════════════════════════
# 0b. Node registry — persisted to .nodes.json
# ════════════════════════════════════════════════════════════════════════════


@dataclass
class NodeConfig:
    id: str
    name: str
    type: str  # wifi, serial, ble, lora
    address: str
    hardware: str = "Unknown"
    mac: str = "Unknown"
    active: bool = False
    online: bool = True
    last_seen: float = 0.0

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "address": self.address,
            "active": self.active,
            "online": self.online,
            "hardware": self.hardware,
            "mac": self.mac,
            "last_seen": self.last_seen,
        }

    @staticmethod
    def from_dict(d: dict) -> "NodeConfig":
        return NodeConfig(
            id=d["id"],
            name=d["name"],
            type=d["type"],
            address=d["address"],
            active=d.get("active", False),
            online=d.get("online", True),
            hardware=d.get("hardware", "Unknown"),
            mac=d.get("mac", "Unknown"),
            last_seen=d.get("last_seen", time.time()),
        )


class NodeRegistry:
    def __init__(self) -> None:
        self._nodes: list[NodeConfig] = []
        self._load()

    def _load(self) -> None:
        if NODES_FILE.exists():
            try:
                data = json.loads(NODES_FILE.read_text())
                self._nodes = [NodeConfig.from_dict(n) for n in data]
            except Exception:
                pass

    def _save(self) -> None:
        try:
            NODES_FILE.write_text(
                json.dumps([n.to_dict() for n in self._nodes], indent=2)
            )
        except Exception as e:
            print(f"[nodes] Save failed: {e}")

    def list(self) -> list[NodeConfig]:
        return list(self._nodes)

    def add(self, name: str, type_: str, address: str, online: bool = True, hardware: str = "Unknown", mac: str = "Unknown") -> NodeConfig:
        # Upsert logic: Priority 1 - MAC (Globally Unique)
        existing = None
        if mac and mac != "Unknown":
            existing = next((n for n in self._nodes if n.mac == mac), None)
            
        # Priority 2 - Serial Port (COMx is stable for this machine)
        if not existing and type_ == "serial":
            existing = next((n for n in self._nodes if n.address == address and n.type == "serial"), None)
            
        # Priority 3 - WiFi Address (IP-based lookup for transient WiFi discovery before MAC/NID probe)
        if not existing and type_ == "wifi":
            existing = next((n for n in self._nodes if n.address == address and n.type == "wifi"), None)
            
        if existing:
            changed = False
            # Update address if it changed (DHCP rotation)
            if existing.address != address:
                print(f"[nodes] Updating {existing.name} (MAC:{existing.mac}) address: {existing.address} -> {address}")
                existing.address = address
                changed = True
            
            # Update name if it's currently a placeholder or UUID
            if name and name != "Unknown" and (existing.name == existing.id or existing.name.startswith("COM")):
                print(f"[nodes] Promoting node {existing.id} name: {existing.name} -> {name}")
                existing.name = name
                changed = True
            
            # Update hardware/mac if we just learned them
            if hardware and hardware != "Unknown":
                existing.hardware = hardware
            if mac and mac != "Unknown":
                existing.mac = mac
            
            existing.last_seen = time.time()
            existing.online = True
            if changed:
                self._save()
            return existing

        # New Node
        nid = mac if mac and mac != "Unknown" else address
        new_node = NodeConfig(
            id=nid, name=name, type=type_, address=address, 
            hardware=hardware, mac=mac, online=online,
            last_seen=time.time() if online else 0
        )
        self._nodes.append(new_node)
        self._save()
        print(f"[nodes] Registered new node: {name} (MAC:{mac})")
        return new_node

    def add_node(self, id: str, name: str, type: str, address: str, hardware: str = "Unknown") -> None:
        for n in self._nodes:
            if n.id == id:
                n.online = True
                n.last_seen = time.time()
                if hardware != "Unknown":
                    n.hardware = hardware
                self._save()
                return
        
        self._nodes.append(NodeConfig(id=id, name=name, type=type, address=address, hardware=hardware))
        self._save()

    def mark_offline(self, name: str, type_: str) -> None:
        target = next((n for n in self._nodes if n.name == name and n.type == type_), None)
        if target and target.online:
            target.online = False
            print(f"[nodes] Node {name} went OFFLINE")
            self._save()

    def mark_all_offline(self) -> None:
        """Called on startup to ensure we only show what's actually reachable."""
        for n in self._nodes:
            n.online = False
        self._save()

    def prune(self) -> int:
        """Remove any node currently marked offline."""
        count = len(self._nodes)
        self._nodes = [n for n in self._nodes if n.online]
        self._save()
        return count - len(self._nodes)

    def remove(self, node_id: str) -> bool:
        before = len(self._nodes)
        self._nodes = [n for n in self._nodes if n.id != node_id]
        if len(self._nodes) < before:
            self._save()
            return True
        return False

    def set_active(self, node_id: str) -> Optional[NodeConfig]:
        target = next((n for n in self._nodes if n.id == node_id), None)
        if not target:
            return None
        for n in self._nodes:
            n.active = n.id == node_id
        self._save()
        return target

    def get(self, node_id: str) -> Optional[NodeConfig]:
        """Get a node by ID, MAC, or address without changing active state."""
        return next((n for n in self._nodes if n.id == node_id or n.mac == node_id or n.address == node_id), None)

    def active_node(self) -> Optional[NodeConfig]:
        return next((n for n in self._nodes if n.active), None)


# ════════════════════════════════════════════════════════════════════════════
# 0c. Sequence registry — persisted to .sequences.json
# ════════════════════════════════════════════════════════════════════════════


@dataclass
class TaskSpec:
    name: str
    type: str
    pin: int
    interval: int
    duration: int = 0

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "type": self.type,
            "pin": self.pin,
            "interval": self.interval,
            "duration": self.duration,
        }

    @staticmethod
    def from_dict(d: dict) -> "TaskSpec":
        return TaskSpec(
            name=d["name"],
            type=d["type"],
            pin=int(d["pin"]),
            interval=int(d["interval"]),
            duration=int(d.get("duration", 0)),
        )


@dataclass
class Sequence:
    name: str
    tasks: list[TaskSpec] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"name": self.name, "tasks": [t.to_dict() for t in self.tasks]}

    @staticmethod
    def from_dict(d: dict) -> "Sequence":
        return Sequence(
            name=d["name"], tasks=[TaskSpec.from_dict(t) for t in d.get("tasks", [])]
        )


class SequenceRegistry:
    def __init__(self) -> None:
        self._seqs: dict[str, Sequence] = {}
        self._load()

    def _load(self) -> None:
        if SEQUENCES_FILE.exists():
            try:
                data = json.loads(SEQUENCES_FILE.read_text())
                self._seqs = {s["name"]: Sequence.from_dict(s) for s in data}
            except Exception:
                pass

    def _save(self) -> None:
        try:
            SEQUENCES_FILE.write_text(
                json.dumps([s.to_dict() for s in self._seqs.values()], indent=2)
            )
        except Exception as e:
            print(f"[sequences] Save failed: {e}")

    def list(self) -> list[Sequence]:
        return list(self._seqs.values())

    def save(self, seq: Sequence) -> None:
        self._seqs[seq.name] = seq
        self._save()

    def delete(self, name: str) -> bool:
        if name in self._seqs:
            del self._seqs[name]
            self._save()
            return True
        return False

    def get(self, name: str) -> Optional[Sequence]:
        return self._seqs.get(name)


# ════════════════════════════════════════════════════════════════════════════
# 1. DeviceState — shared live state
# ════════════════════════════════════════════════════════════════════════════


@dataclass
class DeviceState:
    status: dict = field(default_factory=dict)
    transport: str = "disconnected"  # "http" | "ble" | "disconnected"
    http_ok: bool = False
    ble_ok: bool = False  # True if any peer connected
    ble1_ok: bool = False  # peer A
    ble2_ok: bool = False  # peer B
    peer_names: list = field(default_factory=list)
    last_update: float = 0.0
    http_failures: int = 0  # consecutive HTTP failures
    settings: dict = field(default_factory=lambda: {"transport_strategy": "http_first"})
    serial_ok: bool = False
    serial_port: Optional[str] = None
    active_node: Optional[str] = None  # display name of active node
    active_type: Optional[str] = None  # "wifi" | "serial" | "ble" | "lora" (new)
    active_ip: Optional[str] = None  # routable IP for active WiFi/LoRa node
    discovered_devices: list = field(default_factory=list)  # from network scan
    discovery_time: float = 0.0  # timestamp of last discovery scan
    espnow_ok: bool = False
    tp_mode: str = "J" # "J"=JSON, "C"=CSV, "K"=KV, "B"=BIN (default JSON)
    sim_running: bool = False
    ai_status: str = "idle" # "idle" | "querying" | "error"



# ════════════════════════════════════════════════════════════════════════════
# 2. AI Gateway — Ollama LLM Bridge
# ════════════════════════════════════════════════════════════════════════════


class AIGateway:
    """Handles asynchronous communication with the local Ollama instance."""

    def __init__(self, model: str = "qwen2.5-coder:14b") -> None:
        self.api_url = os.environ.get("OLLAMA_API", "http://localhost:11434/api/generate")
        self.model = model
        self.max_len = 200

    async def query_ai(self, prompt: str) -> str:
        """Fetch a response from local Ollama instance."""
        print(f"[AI] Prompting {self.model}: '{prompt[:30]}...'")
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(
                    self.api_url,
                    json={
                        "model": self.model,
                        "prompt": prompt,
                        "stream": False
                    },
                    timeout=aiohttp.ClientTimeout(total=45.0)
                ) as r:
                    if r.status == 200:
                        data = await r.json()
                        result = data.get('response', '').strip()
                        # Clean for mesh: no newlines, limited length
                        result = result.replace('\n', ' ').replace('\r', '')
                        if len(result) > self.max_len:
                            result = result[:self.max_len-3] + "..."
                        return result
                    else:
                        return f"AI_ERR: HTTP {r.status}"
        except asyncio.TimeoutError:
            return "AI_ERR: Timeout"
        except Exception as e:
            return f"AI_ERR: {str(e)[:40]}"


# ════════════════════════════════════════════════════════════════════════════
# 2b. WebSocketManager — browser connection pool
# ════════════════════════════════════════════════════════════════════════════


class WebSocketManager:
    def __init__(self) -> None:
        self._connections: Set[WebSocket] = set()

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._connections.add(ws)

    def disconnect(self, ws: WebSocket) -> None:
        self._connections.discard(ws)

    async def broadcast(self, data: dict) -> None:
        dead: Set[WebSocket] = set()
        for ws in self._connections:
            try:
                await ws.send_json(data)
            except Exception:
                dead.add(ws)
        self._connections -= dead


# ════════════════════════════════════════════════════════════════════════════
# 2c. mDNS Discovery Engine
# ════════════════════════════════════════════════════════════════════════════


class MagicListener(ServiceListener):
    def __init__(self, registry: NodeRegistry, discovered_list: list) -> None:
        self.registry = registry
        self.discovered = discovered_list

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        info = zc.get_service_info(type_, name)
        if info and info.properties:
            ip = socket.inet_ntoa(info.addresses[0])
            
            # Safe property decoding
            def get_prop(key, default=""):
                val = info.properties.get(key.encode() if isinstance(key, str) else key)
                return val.decode("utf-8") if val else default

            # Filter to Magic-compatible devices
            dev_type = get_prop("type", "unknown").lower()
            is_magic = "magic" in dev_type or dev_type == "gateway" or "magic" in name.lower()
            if not is_magic:
                return

            node_id = get_prop("id") or name.split(".")[0]
            
            # Automatically register/update in persistent registry
            hw = get_prop("hw", "Unknown")
            mac = get_prop("mac", "Unknown")
            
            # Use MAC as stable identity
            self.registry.add(node_id, "wifi", ip, online=True, hardware=hw, mac=mac)
            
            # Update transient list for backward compat / immediate UI response
            if not any(d["address"] == ip for d in self.discovered):
                self.discovered.append({
                    "name": node_id,
                    "id": mac if mac != "Unknown" else node_id,
                    "address": ip,
                    "type": get_prop("type", "gateway"),
                    "ver": get_prop("ver", "v2.0.0"),
                    "mac": mac,
                    "last_seen": time.time()
                })
                print(f"[mDNS] Found: {node_id} (MAC:{mac}) at {ip}")

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        pass

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        node_id = name.split(".")[0]
        self.registry.mark_offline(node_id, "wifi")
        print(f"[mDNS] Service removed: {node_id}")


# ════════════════════════════════════════════════════════════════════════════
# 2b. SerialLink — sync serial wrapped for asyncio
# ════════════════════════════════════════════════════════════════════════════


class SerialLink:
    """Wraps a pyserial connection for use alongside BLE and HTTP transports."""

    def __init__(self, port: str, baud: int = 115200) -> None:
        self._port = port
        self._baud = baud
        self._ser: Any = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._read_task: Optional[asyncio.Task] = None
        self.line_queue: asyncio.Queue = asyncio.Queue()

    async def connect(self) -> None:
        if not PYSERIAL:
            raise RuntimeError("pyserial not installed")
        self._loop = asyncio.get_event_loop()
        await self._loop.run_in_executor(None, self._open)
        
        # Start background read loop
        self._read_task = asyncio.create_task(self._read_loop())
        
        # Prime the board to ensure serial streaming is enabled
        await self.send_command("STREAM ON")
        print(f"[Serial] Connected to {self._port} and streaming enabled")

    def _open(self) -> None:
        self._ser = serial.Serial(self._port, self._baud, timeout=1)

    async def disconnect(self) -> None:
        if self._read_task:
            self._read_task.cancel()
        if self._ser and self._ser.is_open:
            self._ser.close()
        print(f"[Serial] Disconnected from {self._port}")

    async def _read_loop(self) -> None:
        """Background loop reading lines from Serial."""
        while self._ser and self._ser.is_open:
            try:
                line = await self._loop.run_in_executor(None, self._read_line)  # type: ignore
                if line:
                    await self.line_queue.put(line)
            except asyncio.CancelledError:
                break
            except Exception as e:
                print(f"[Serial] Read error: {e}")
                await asyncio.sleep(1)
                
    def _read_line(self) -> str:
        """Synchronous read call to be run in executor."""
        if not self._ser or not self._ser.is_open:
            return ""
        try:
            return self._ser.readline().decode('utf-8', errors='ignore').strip()
        except:
            return ""

    async def send_command(self, cmd: str) -> bool:
        if not self.is_connected or not self._loop:
            return False
        try:
            data = (cmd.strip() + "\n").encode()
            if self._ser:
                await self._loop.run_in_executor(None, self._ser.write, data)
                return True
            return False
        except Exception as e:
            print(f"[Serial] Write error: {e}")
            return False

    @property
    def is_connected(self) -> bool:
        return bool(self._ser and self._ser.is_open)

    @property
    def device_name(self) -> str:
        return self._port

class SerialHub:
    """Background monitor that scans for NEW serial ports and adds them to registry."""
    def __init__(self, registry: NodeRegistry, poller: 'StatusPoller') -> None:
        self.registry = registry
        self.poller = poller
        self._running = False
        self._known_ports = set()
        self._task = None

    async def start(self):
        self._running = True
        self._task = asyncio.create_task(self._run())
        print("[SerialHub] Started discovery & monitor")

    def stop(self):
        self._running = False
        if self._task:
            self._task.cancel()

    async def _run(self):
        while self._running:
            try:
                if PYSERIAL:
                    import serial.tools.list_ports
                    ports = serial.tools.list_ports.comports()
                    for p in ports:
                        if p.device in self._known_ports:
                            continue
                            
                        # Mark as known immediately to prevent spam
                        self._known_ports.add(p.device)
                        
                        # Filter for Heltec USB-to-UART bridges or generic USB Serial (ESP32-S3 direct)
                        # We also check the hardware ID for better precision
                        desc = (p.description or "").upper()
                        hwid = (p.hwid or "").upper()
                        
                        is_magic = (
                            any(sig in desc for sig in ["CP210", "USB SERIAL", "CH340", "ESP32", "HELTEC"]) or \
                            any(vid in hwid for vid in ["303A", "10C4", "1A86", "2341"])  # 2341 = Arduino
                        )
                        
                        if is_magic:
                            print(f"[SerialHub] New Magic port detected: {p.device} ({p.description})")
                            # Auto-add to registry if not already there
                            node = self.registry.add(p.device, "serial", p.device, online=True)
                            
                            # Fire-and-forget probe for hardware version
                            asyncio.create_task(self._probe_node(p.device, node))
                            
                await asyncio.sleep(5)
            except Exception as e:
                print(f"[SerialHub] Error: {e}")
                await asyncio.sleep(10)

    async def _probe_node(self, port: str, node: NodeConfig):
        """Temporary connect to probe hardware version and node ID."""
        link = SerialLink(port)
        try:
            await link.connect()
            # Wait for any boot text or send STATUS
            await link.send_command("STATUS")
            
            # Brief wait for response
            start_time = time.time()
            while time.time() - start_time < 3:
                if not link.line_queue.empty():
                    line = await link.line_queue.get()
                    # Look for hardware signature in status or boot log
                    # Example: "HV: V4" or part of status JSON
                    if '"hw":"' in line or '"mac":"' in line:
                        import json
                        try:
                            data = json.loads(line)
                            hw = data.get("hw", "Unknown")
                            mac = data.get("mac", "Unknown")
                            node.hardware = hw
                            if mac != "Unknown":
                                node.mac = mac
                            self.registry._save()
                            print(f"[SerialHub] Probed {port}: Hardware={hw}, MAC={mac}")
                            break
                        except: pass
                    elif "Magic V" in line:
                        # Fallback for boot strings
                        if "V4" in line: node.hardware = "V4"
                        elif "V3" in line: node.hardware = "V3"
                        elif "V2" in line: node.hardware = "V2"
                        self.registry._save()
                        break
                await asyncio.sleep(0.1)
            await link.disconnect()
        except Exception as e:
            print(f"[SerialHub] Probe failed for {port}: {e}")


# ════════════════════════════════════════════════════════════════════════════
class TransportManager:
    def __init__(
        self,
        state: DeviceState,
        device_ip: Optional[str],
        peers: list,
        serial_link: Any = None,
        sim_bridge: Any = None,
        registry: Any = None
    ) -> None:
        self.state = state
        self.device_ip = device_ip
        self._peers = peers
        self._session: Any = None
        self._round_counter: int = 0
        self._serial: Any = serial_link
        self._sim: Any = sim_bridge
        self.registry = registry
        self._serial_cache: dict[str, Any] = {}
        self._serial_failures: dict[str, float] = {}  # port -> timestamp
        if serial_link and hasattr(serial_link, "_port"):
            self._serial_cache[serial_link._port] = serial_link

    async def _get_serial_link(self, port: str) -> Optional[Any]:
        """Get or open a serial link with failure cooldown to prevent thrashing."""
        if port in self._serial_cache:
            link = self._serial_cache[port]
            if link and link.is_connected:
                return link
            # Stale connection in cache? Clean it up
            del self._serial_cache[port]

        # Cooldown check: don't retry a failing port for 30 seconds
        last_fail = self._serial_failures.get(port, 0)
        if time.time() - last_fail < 30:
            return None

        # Only print opening log if we're tracing or it's new
        if port not in self._serial_failures:
            print(f"[Transport] Opening dynamic serial link to {port}...")
        
        link = SerialLink(port)
        try:
            # Short timeout for initial connect to prevent API hang
            await asyncio.wait_for(link.connect(), timeout=3.0)
            self._serial_cache[port] = link
            self._serial_failures.pop(port, None) # Clear failure
            return link
        except Exception as e:
            if time.time() - last_fail > 60: # Only print periodically
                print(f"[Transport] Port {port} skipped: {str(e)[:40]}")
            self._serial_failures[port] = time.time()
            return None

    async def send_command(self, cmd: str, node_id: Optional[str] = None) -> bool:
        # 1. Resolve target metadata
        target_type = None
        target_ip = None
        target_port = None
        
        if node_id:
             if node_id.startswith(("COM", "/dev/")):
                 target_port = node_id
                 target_type = "serial"
             elif self.registry:
                 node = self.registry.get(node_id)
                 if node:
                     target_type = node.type
                     if target_type == "serial":
                         target_port = node.address
                     elif target_type in ("wifi", "http"):
                         target_ip = node.address
                         target_type = "http"
                         
             # Mesh Routing Injection: If the target node is NOT the active directly-connected gateway
             # and the user hasn't manually prefixed the command, prepend the devicename so the 
             # gateway firmware can forward it according to the 3-hop mesh rules.
             if node_id != self.state.active_node and node_id not in ("ALL",):
                 # Don't double-prefix if already prefixed
                 if not cmd.startswith(f"{node_id} "):
                     cmd = f"{node_id} {cmd}"

        transport = await self.pick_transport(cmd, target_type=target_type)
        wire_cmd = self._serialize_command(cmd, transport)
        
        if transport == "sim":
            return await self._sim.send_command(wire_cmd)

        if transport == "serial":
            if not wire_cmd.endswith("\n"):
                wire_cmd += "\n"
            
            # Resolve physical link
            link = None
            if target_port:
                link = await self._get_serial_link(target_port)
            else:
                link = self._serial # Primary gateway fallback
            
            if not link or not link.is_connected:
                return False
            ok = await link.send_command(wire_cmd)
            # Update state for UI feedback (only if it's the active/gateway link)
            if link == self._serial or (target_port and target_port == self.state.serial_port):
                self.state.serial_ok = ok
                self.state.transport = "serial" if ok else "disconnected"
            return ok

        if transport == "http":
            ok = await self._send_http(cmd, ip=target_ip if node_id else None)
            if ok:
                self.state.http_failures = 0
                self.state.http_ok = True
                self.state.transport = "http"
                return True
            self.state.http_failures += 1
            self.state.http_ok = False
            return False

        ok = await self._send_ble(cmd)
        self.state.ble_ok = ok
        self.state.transport = "ble" if ok else "disconnected"
        return ok

    def _serialize_command(self, cmd: str, transport: str) -> str:
        """Normalize to format-on-the-wire based on tp_mode and transport."""
        # For Serial, we often want raw text regardless of other settings unless forced
        if transport == "serial" and self.state.tp_mode == "B":
            return cmd
            
        if self.state.tp_mode == "J":
            parts = cmd.split(None, 1)
            c = parts[0]
            a = parts[1] if len(parts) > 1 else ""
            return json.dumps({"cmd": c, "args": a})
        elif self.state.tp_mode == "C":
            return ",".join(cmd.split())
        elif self.state.tp_mode == "K":
            parts = cmd.split(None, 1)
            c = parts[0]
            a = parts[1] if len(parts) > 1 else ""
            return f"CMD={c} ARGS={a}"
        return cmd

    async def pick_transport(self, cmd: str, target_type: Optional[str] = None) -> str:
        if self.state.sim_running and self._sim:
            return "sim"
        strategy = self.state.settings.get("transport_strategy", "http_first")
        _ip = self.state.active_ip or self.device_ip
        http_ok = bool(_ip and AIOHTTP)
        if target_type:
            if target_type == "serial" and self.state.serial_ok: return "serial"
            if target_type == "http" and http_ok: return "http"
            if target_type == "ble": return "ble"
        if strategy == "ble_only": return "ble"
        if self.state.active_type == "serial" and self.state.serial_ok: return "serial"
        if http_ok and self.state.http_failures < 3: return "http"
        return "ble"

    async def _send_http(self, cmd: str, ip: Optional[str] = None) -> bool:
        ip = ip or self.state.active_ip or self.device_ip
        if not AIOHTTP or not ip: return False
        try:
            session = await self._get_session()
            async with session.post(f"http://{ip}/api/cmd", data={"cmd": cmd}, timeout=aiohttp.ClientTimeout(total=3.0)) as r:
                return r.status == 200
        except Exception: return False

    async def _send_ble(self, cmd: str) -> bool:
        if not self._peers: return False
        try:
            for ble in self._peers:
                if ble.is_connected:
                    await ble.send_command(cmd)
            return True
        except Exception: return False

    async def _get_session(self) -> Any:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession()
        return self._session

    async def close(self) -> None:
        if self._session and not self._session.closed:
            await self._session.close()




# ════════════════════════════════════════════════════════════════════════════
# 4. StatusPoller — background task pushing device state to WS clients
# ════════════════════════════════════════════════════════════════════════════


class StatusPoller:
    INTERVAL = 1.5  # seconds between polls

    def __init__(
        self,
        state: DeviceState,
        ws_manager: WebSocketManager,
        device_ip: Optional[str],
        peers: list,  # list[BLELink] — 0, 1, or 2 peers
        node_reg: Any = None,
        sim_bridge: Any = None,
        transport: Any = None
    ) -> None:
        self.state = state
        self.ws_manager = ws_manager
        self.device_ip = device_ip
        self._peers = peers
        self._session: Any = None
        self._running = False
        self.registry = node_reg
        self._sim: Any = sim_bridge
        self._transport = transport
        self._trigger = asyncio.Event()
        self._daemon_client: Any = None  # Set by build_app after construction

    def trigger(self) -> None:
        self._trigger.set()

    async def run(self) -> None:
        """Main loop: Poll active device status periodically."""
        self._running = True
        while self._running:
            # Wait for interval OR trigger
            try:
                await asyncio.wait_for(self._trigger.wait(), timeout=self.INTERVAL)
                self._trigger.clear()
            except asyncio.TimeoutError:
                pass

            status = None
            
            # 1. HTTP poll
            status = await self._poll_http()
            
            # 2. Simulator fallback
            if self.state.sim_running and self._sim:
                s = await self._sim.get_status()
                if s: 
                    status = s
                    status["_via"] = "sim"
                    self.state.transport = "sim"

            if status:
                self.state.status = status
                self.state.last_update = time.time()
                await self.ws_manager.broadcast(
                    {
                        "type": "status",
                        "peer": "A",
                        "transport": self.state.transport,
                        "active_node": self.state.active_node,
                        "http_ok": self.state.http_ok,
                        "ble_ok": self.state.ble_ok,
                        **status,
                    }
                )
            
            # 1. HTTP poll
            status = await self._poll_http()
            
            # 2. Simulator fallback
            if not status and self._sim and self._sim._running:
                status = await self._sim.get_status()
                if status: status["_via"] = "sim"

            if status:
                self.state.status = status
                self.state.last_update = time.time()
                await self.ws_manager.broadcast(
                    {
                        "type": "status",
                        "peer": "A",
                        "transport": self.state.transport,
                        "active_node": self.state.active_node,
                        "http_ok": self.state.http_ok,
                        "ble_ok": self.state.ble_ok,
                        **status,
                    }
                )

            # BLE poll — all peers in parallel; each broadcast tagged with peer label
            if self._peers:
                ble_results = await asyncio.gather(
                    *[self._poll_ble_peer(ble, i) for i, ble in enumerate(self._peers)],
                    return_exceptions=True,
                )
                for i, result in enumerate(ble_results):
                    if isinstance(result, dict) and result:
                        peer_label = chr(ord("A") + i)
                        if not status:  # only update primary status if HTTP missed
                            self.state.status = result
                            self.state.last_update = time.time()
                        await self.ws_manager.broadcast(
                            {
                                "type": "status",
                                "peer": peer_label,
                                "transport": "ble",
                                "active_node": self.state.active_node,
                                "http_ok": self.state.http_ok,
                                "ble_ok": self.state.ble_ok,
                                **result,
                            }
                        )

            # 3. Serial poll (Discovery for all nodes, fallback for active)
            if self.registry and self._transport:
                for node in self.registry.list():
                    if node.type == "serial" and node.address:
                        # Poll nodes that are placeholders or missing MACs
                        if node.name == node.id or node.name.startswith("COM") or node.mac == "Unknown":
                            await self._transport.send_command("STATUS", node_id=node.address)
                        elif node.id == self.state.active_node:
                            # Also poll the active node even if named
                            await self._transport.send_command("STATUS", node_id=node.address)

            # 4. Daemon node list — broadcast to WebSocket clients
            try:
                daemon_nodes = await self._daemon_client.list_nodes() if self._daemon_client else []
                await self.ws_manager.broadcast({
                    "type": "daemon_nodes",
                    "daemon_nodes": daemon_nodes,
                    "daemon_connected": len(daemon_nodes) >= 0,  # True if call succeeded
                })
            except Exception:
                await self.ws_manager.broadcast({
                    "type": "daemon_nodes",
                    "daemon_nodes": [],
                    "daemon_connected": False,
                })

            await asyncio.sleep(self.INTERVAL)

    def stop(self) -> None:
        self._running = False

    async def _poll_http(self) -> Optional[dict]:
        ip = self.state.active_ip or self.device_ip
        if not AIOHTTP or not ip:
            return None
        try:
            session = await self._get_session()
            async with session.get(
                f"http://{ip}/api/status",
                timeout=aiohttp.ClientTimeout(total=2.5),
            ) as r:
                if r.status == 200:
                    data = await r.json(content_type=None)
                    self.state.http_ok = True
                    self.state.http_failures = 0
                    self.state.transport = "http"
                    return data
        except Exception:
            pass
        self.state.http_ok = False
        self.state.http_failures += 1
        return None

    async def _poll_ble_peer(self, ble: "BLELink", idx: int) -> Optional[dict]:
        """Poll a single BLE peer, update per-peer connection state."""
        if not BLEAK or not ble.is_connected:
            if idx == 0:
                self.state.ble1_ok = False
            elif idx == 1:
                self.state.ble2_ok = False
            self.state.ble_ok = self.state.ble1_ok or self.state.ble2_ok
            if not self.state.ble_ok:
                self.state.transport = "disconnected"
            return None
        await ble.send_command("STATUS")
        if idx == 0:
            self.state.ble1_ok = True
        elif idx == 1:
            self.state.ble2_ok = True
        self.state.ble_ok = True
        self.state.transport = "ble"
        existing = self.state.status or {}
        return {**existing, "_via": "ble", "_peer": ble.device_name}

    async def _get_session(self) -> Any:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession()
        return self._session

    async def close(self) -> None:
        if self._session and not self._session.closed:
            await self._session.close()


# ════════════════════════════════════════════════════════════════════════════
# 5. HTTP proxy helpers (device API → browser)
# ════════════════════════════════════════════════════════════════════════════


async def _proxy_get(device_ip: Optional[str], path: str) -> Optional[dict]:
    if not AIOHTTP or not device_ip:
        return None
    try:
        async with aiohttp.ClientSession() as s:
            async with s.get(
                f"http://{device_ip}{path}",
                timeout=aiohttp.ClientTimeout(total=3.0),  # type: ignore
            ) as r:
                return await r.json(content_type=None)
    except Exception:
        return None


async def _proxy_post(device_ip: Optional[str], path: str, data: dict) -> bool:
    if not AIOHTTP or not device_ip:
        return False
    try:
        async with aiohttp.ClientSession() as s:
            async with s.post(
                f"http://{device_ip}{path}",
                data=data,
                timeout=aiohttp.ClientTimeout(total=3.0),  # type: ignore
            ) as r:
                return r.status == 200
    except Exception:
        return False


async def _send_cmd_to_ip(ip: str, cmd: str) -> bool:
    """Send a single command directly to a specific device IP, bypassing TransportManager."""
    if not AIOHTTP or not ip:
        return False
    try:
        async with aiohttp.ClientSession() as s:
            async with s.post(
                f"http://{ip}/api/cmd",
                data={"cmd": cmd},
                timeout=aiohttp.ClientTimeout(total=4.0),  # type: ignore
            ) as r:
                return r.status == 200
    except Exception:
        return False


# ════════════════════════════════════════════════════════════════════════════
# 6. App factory
# ════════════════════════════════════════════════════════════════════════════


def build_app(
    device_name: str,
    device_ip: Optional[str],
    device_name2: Optional[str] = None,
    no_ble: bool = False,
) -> FastAPI:
    state = DeviceState()
    ws_mgr = WebSocketManager()
    node_reg = NodeRegistry()
    seq_reg = SequenceRegistry()
    CONFIGS_DIR.mkdir(parents=True, exist_ok=True)
    _serial_link: Any = None

    # Singletons populated at startup
    _ble_peers: list = []  # up to 2 BLELink instances
    _transport: Optional[TransportManager] = None
    _serial_hub: Optional[SerialHub] = None
    _poller: Optional[StatusPoller] = None
    _poll_task: Optional[asyncio.Task] = None
    _zc: Optional[Zeroconf] = None
    _browser: Optional[ServiceBrowser] = None
    _sim_bridge: Optional[Any] = None
    _ai_gateway = AIGateway()
    _processor_task: Optional[asyncio.Task] = None
    _daemon_client: BaseDeviceClient = DaemonClient()  # Connects to http://localhost:8001 by default

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        nonlocal _ble_peers, _transport, _poller, _poll_task, _serial_link, _processor_task, _zc, _browser, _sim_bridge, _serial_hub, _daemon_client

        # --- STARTUP ---
        # Initialize active IP to the startup device_ip
        state.active_ip = device_ip

        # Connect daemon client
        await _daemon_client.connect()

        # Load persisted transport strategy
        if SETTINGS_FILE.exists():
            try:
                saved = json.loads(SETTINGS_FILE.read_text())
                if saved.get("transport_strategy") in _TRANSPORT_STRATEGIES:
                    state.settings["transport_strategy"] = saved["transport_strategy"]
                    print(f"[settings] Transport strategy: {state.settings['transport_strategy']}")
            except Exception:
                pass

        # Mark current registry as offline on startup; discovery will toggle back to online
        node_reg.mark_all_offline()
        
        # Restore active node from registry on startup
        an = node_reg.active_node()
        if an:
            state.active_node = an.name
            state.active_type = an.type
            if an.type in ("wifi", "lora") and an.address:
                state.active_ip = an.address
            
            # Auto-connect Serial if it was active
            if an.type == "serial" and PYSERIAL:
                _serial_link = SerialLink(an.address)
                try:
                    await _serial_link.connect()
                    state.serial_ok = _serial_link.is_connected
                    state.serial_port = an.address
                    print(f"[serial] Auto-connected to {an.address}")
                except Exception as e:
                    print(f"[serial] Auto-connect failed: {e}")
                    state.serial_ok = False
            
            print(f"[nodes] Restored active node: {an.name} ({an.type})")
        else:
            state.active_node = None
            state.active_type = "wifi" if state.active_ip else "ble"

        _sim_bridge = None
        if SIMULATOR_AVAILABLE:
            test_script = str(Path(__file__).parent.parent / "test_sim.py")
            _sim_bridge = SimulatorBridge(f"py -u {test_script}")

        # HTTP transport + status poller start immediately
        _transport = TransportManager(state, device_ip, _ble_peers, _serial_link, _sim_bridge, node_reg)
        _poller = StatusPoller(state, ws_mgr, device_ip, _ble_peers, node_reg, _sim_bridge, _transport)
        _poller._daemon_client = _daemon_client
        _poll_task = asyncio.create_task(_poller.run())
        
        # Start unified message processor
        _processor_task = asyncio.create_task(_msg_processor_task())
        
        # Start Serial Discovery Hub
        _serial_hub = SerialHub(node_reg, _poller)
        await _serial_hub.start()
        
        # Ensure the command-line device is in the registry and marked active
        if device_name and device_name.startswith("COM"):
            node_reg.add(device_name, "serial", device_name, online=True)
            state.active_node = device_name
            state.active_type = "serial"
            state.serial_port = device_name

        port = int(os.environ.get("PORT", 8000))
        print(f"[server] Listening at http://localhost:{port}")

        # BLE scan deferred
        if BLEAK and not no_ble:
            asyncio.create_task(_ble_scan())

        # mDNS discovery start
        if ZEROCONF:
            try:
                _zc = Zeroconf()
                listener = MagicListener(node_reg, state.discovered_devices)
                _browser = ServiceBrowser(_zc, "_http._tcp.local.", listener)
                print("[mDNS] Browser started listening for _http._tcp.local.")
            except Exception as e:
                print(f"[mDNS] Failed to start: {e}")

        yield

        # --- SHUTDOWN ---
        if _processor_task:
            _processor_task.cancel()
        if _sim_bridge and _sim_bridge._running:
            await _sim_bridge.stop()
        if _poller:
            _poller.stop()
        if _poll_task:
            _poll_task.cancel()
        if _zc:
            if _browser:
                _browser.cancel()
            _zc.close()
            print("[mDNS] Browser stopped")
        if _transport:
            await _transport.close()
        for ble in _ble_peers:
            try:
                await ble.disconnect()
            except Exception:
                pass
        if _serial_link:
            try:
                await _serial_link.disconnect()
            except Exception:
                pass

        # Disconnect daemon client
        await _daemon_client.disconnect()

    app = FastAPI(title="Magic PC Control", lifespan=lifespan)

    async def _handle_message(line_raw: str, source: str) -> None:
        """Unified handler for all incoming radio/serial traffic."""
        line = line_raw.strip()
        if not line: return
        
        # Broadcast raw line to UI immediately
        await ws_mgr.broadcast({"type": "serial_log", "text": line, "source": source})

        # --- JSON Status Handling ---
        is_json = False
        json_data = None
        
        if "[JSON_STATUS]" in line:
            try:
                raw = line.split("[JSON_STATUS]", 1)[1].strip()
                json_data = json.loads(raw)
                is_json = True
            except Exception: pass
        elif line.startswith("{") and line.endswith("}"):
            try:
                json_data = json.loads(line)
                is_json = True
            except Exception: pass

        if is_json and json_data:
            # Update live state from serial/mesh JSON status
            state.status = json_data
            state.last_update = time.time()
            
            # Update registry with learned MAC/Hardware
            mac = json_data.get("mac", "Unknown")
            hw = json_data.get("hw", "Unknown")
            node_id = json_data.get("id", source)
            
            if node_reg:
                # Upsert based on Source (COM port) or MAC
                type_ = "serial" if (source.startswith("COM") or source == "serial") else "wifi"
                node_reg.add(node_id, type_, source, online=True, hardware=hw, mac=mac)
                
                # Normalize V2 firmware 'peers' to V1 'mesh'
                if "peers" in json_data and "mesh" not in json_data:
                    json_data["mesh"] = json_data.pop("peers")
                    for p in json_data["mesh"]:
                        if "hop" in p: p["hops"] = p.pop("hop")
                        if "last_seen" in p: p["ago"] = p.pop("last_seen")
                
                # Rewrite Mesh IDs to human-readable names before broadcasting
                if "mesh" in json_data:
                    # Create a copy for broadcast to prevent modification during iteration
                    json_data = copy.deepcopy(json_data)
                    for peer in json_data["mesh"]:
                        p_id = peer.get("id") or peer.get("nodeId")
                        if p_id:
                            # Try exact ID or MAC match
                            for known in node_reg.list():
                                if known.id == p_id or known.mac == p_id:
                                    if known.name and known.name != "Unknown" and not known.name.startswith("COM") and known.name != known.id:
                                        peer["id"] = known.name
                                    break
            
            # Broadcast updated status to UI to populate the dashboard
            await ws_mgr.broadcast({
                "type": "status",
                "peer": "A",
                "transport": state.active_type or "serial",
                "active_node": state.active_node,
                "http_ok": state.http_ok,
                **json_data
            })
            return

        # AI Mesh Handling
        if "AI_QUERY:" in line:
            parts = line.split("AI_QUERY:", 1)
            prompt = parts[1].strip() if len(parts) > 1 else ""
            if not prompt:
                return

            print(f"[AI] Request from {source}: {prompt}")
            state.ai_status = "querying"
            await ws_mgr.broadcast({"type": "ai_status", "status": "querying", "prompt": prompt})
            
            # Fetch response from Ollama
            response = await _ai_gateway.query_ai(prompt)
            
            state.ai_status = "idle"
            print(f"[AI] Response: {response}")
            await ws_mgr.broadcast({"type": "ai_status", "status": "idle", "response": response})
            
            # Dispatch back to mesh
            if _transport:
                # Use broadcast mode to ensure it reaches the requesting node
                await _transport.send_command(f"ALL AI: {response}")

    async def _msg_processor_task() -> None:
        """Background loop to drain line queues from all sources."""
        while True:
            try:
                # Drain Main Serial
                if _serial_link:
                    while not _serial_link.line_queue.empty():
                        line = await _serial_link.line_queue.get()
                        port_id = getattr(_serial_link, "_port", "serial")
                        await _handle_message(line, port_id)
                
                # Drain All Serial Links in Cache (Multi-node support)
                if _transport and hasattr(_transport, "_serial_cache"):
                    for port, link in _transport._serial_cache.items():
                        while not link.line_queue.empty():
                            line = await link.line_queue.get()
                            await _handle_message(line, port)

                # Drain Simulator
                if _sim_bridge and _sim_bridge._running:
                    while not _sim_bridge.line_queue.empty():
                        line = await _sim_bridge.line_queue.get()
                        await _handle_message(line, "sim")
                
            except Exception as e:
                print(f"[processor] Error: {e}")
            
            await asyncio.sleep(0.1)

    def _effective_ip() -> Optional[str]:
        """Resolve the effective IP: active_ip (if set) overrides startup device_ip."""
        return state.active_ip or device_ip

    # ── Startup / Shutdown ────────────────────────────────────────────────

    async def _ble_scan() -> None:
        """Background BLE scan — runs after HTTP is already serving."""
        try:
            prefixes = [p for p in [device_name, device_name2] if p]
            print(f"[BLE] Scanning for {prefixes} (10s)…")
            all_devs = await BleakScanner.discover(timeout=10.0)
            matched_addrs: set = set()
            matches: list = []
            for prefix in prefixes:
                for d in all_devs:
                    if (
                        d.name
                        and d.name.strip()
                        and d.name.startswith(prefix)
                        and d.address not in matched_addrs
                    ):
                        matches.append(d)
                        matched_addrs.add(d.address)
            print(f"[BLE] Found {len(matches)} matching device(s)")
            for i, dev in enumerate(matches[:2]):
                buf = ResponseBuffer()
                cfg = BLEConfig(device_name_prefix=device_name)
                ble = BLELink(cfg, buf)
                try:
                    await ble.connect(dev)
                    _ble_peers.append(ble)
                    state.peer_names = state.peer_names + [dev.name]
                    print(f"[BLE] Peer {chr(65 + i)}: {dev.name} ✓")
                except Exception as e:
                    print(f"[BLE] Peer {i + 1} ({dev.name}) failed: {e}")
            state.ble1_ok = len(_ble_peers) >= 1 and _ble_peers[0].is_connected
            state.ble2_ok = len(_ble_peers) >= 2 and _ble_peers[1].is_connected
            state.ble_ok = bool(_ble_peers)
            if _poller:
                _poller._peers = list(_ble_peers)
            if _transport:
                _transport._peers = list(_ble_peers)
        except Exception as e:
            print(f"[BLE] Scan failed: {e} — BLE disabled")


    # ── Static files ──────────────────────────────────────────────────────

    app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

    @app.get("/", response_class=FileResponse)
    async def _root() -> FileResponse:
        return FileResponse(STATIC_DIR / "index.html")

    @app.get("/protodev", response_class=FileResponse)
    async def _protodev() -> FileResponse:
        return FileResponse(STATIC_DIR / "protodev.html")

    # ── WebSocket ─────────────────────────────────────────────────────────

    @app.websocket("/ws")
    async def _ws_endpoint(ws: WebSocket) -> None:
        await ws_mgr.connect(ws)
        # Push current state immediately on connect
        if state.status:
            await ws.send_json(
                {
                    "type": "status",
                    "peer": "A",
                    "transport": state.transport,
                    "http_ok": state.http_ok,
                    "ble_ok": state.ble_ok,
                    "ble1_ok": state.ble1_ok,
                    "ble2_ok": state.ble2_ok,
                    "peer_names": state.peer_names,
                    **state.status,
                }
            )
        try:
            while True:
                await ws.receive_text()  # keep connection alive; ignore client pings
        except WebSocketDisconnect:
            ws_mgr.disconnect(ws)

    # ── Command endpoint ──────────────────────────────────────────────────

    @app.post("/api/cmd")
    async def _cmd(body: dict) -> PlainTextResponse:
        cmd = (body.get("cmd") or "").strip()
        node_id = body.get("node_id")

        if not cmd:
            return PlainTextResponse("Missing cmd", status_code=400)

        # If node_id is provided, try daemon routing first (daemon nodes take priority)
        if node_id and _daemon_client:
            try:
                daemon_nodes = await _daemon_client.list_nodes()
                daemon_ids = {n.get("id") for n in daemon_nodes}

                # ALL = broadcast to every daemon-registered node in parallel
                if node_id == "ALL":
                    import asyncio as _asyncio
                    results = await _asyncio.gather(
                        *[_daemon_client.send_command(n.get("id"), cmd) for n in daemon_nodes],
                        return_exceptions=True
                    )
                    ok_count = sum(1 for r in results if r is True)
                    if _poller:
                        _poller.trigger()
                    return PlainTextResponse(
                        f"ALL:{ok_count}/{len(daemon_nodes)}",
                        status_code=200
                    )

                if node_id in daemon_ids:
                    ok = await _daemon_client.send_command(node_id, cmd)
                    if _poller:
                        _poller.trigger()
                    return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)
            except Exception as e:
                print(f"[cmd] Daemon routing failed for {node_id}: {e}, falling through to direct transport")

        # Fall through to direct transport (existing behaviour)
        if _transport is None:
            return PlainTextResponse("No transport available", status_code=503)

        ok = await _transport.send_command(cmd, node_id=node_id)
        if ok and _poller:
            _poller.trigger()
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.post("/api/test/transform")
    async def _test_transform(body: dict) -> JSONResponse:
        """Trace a command's transformation from string to wire format to firmware parse."""
        cmd = (body.get("cmd") or "").strip()
        node_id = body.get("node_id")

        if not cmd:
            return JSONResponse({"ok": False, "error": "Missing cmd"}, status_code=400)
        if _transport is None:
            return JSONResponse({"ok": False, "error": "No transport available"}, status_code=503)

        # 1. Capture local transformation
        transport_name = await _transport.pick_transport(cmd)
        wire_cmd = _transport._serialize_command(cmd, transport_name)
        
        # 2. Wrap in TRANSFORM for firmware-side verification
        transform_query = f"TRANSFORM {wire_cmd}"
        
        ok = await _transport.send_command(transform_query, node_id=node_id)
        if ok and _poller:
            _poller.trigger()
            
        return JSONResponse({
            "ok": ok,
            "local_trace": {
                "original": cmd,
                "transport": transport_name,
                "tp_mode": state.tp_mode,
                "wire_format": wire_cmd
            }
        })

    @app.post("/api/transport/mode")
    async def _set_tp_mode(body: dict) -> JSONResponse:
        mode = body.get("mode", "J").upper()
        if mode in ("J", "C", "K", "B"):
            state.tp_mode = mode
            print(f"[transport] Wire format set to: {mode}")
            return JSONResponse({"ok": True, "mode": mode})
        return JSONResponse({"ok": False, "error": "Invalid mode"}, status_code=400)

    # ── Transport status ──────────────────────────────────────────────────

    @app.get("/api/transport")
    async def _transport_status() -> JSONResponse:
        return JSONResponse(
            {
                "transport": state.transport,
                "http_ok": state.http_ok,
                "ble_ok": state.ble_ok,
                "ble1_ok": state.ble1_ok,
                "ble2_ok": state.ble2_ok,
                "peer_names": state.peer_names,
                "http_failures": state.http_failures,
                "last_update": round(time.time() - state.last_update, 1)
                if state.last_update
                else -1,
                "serial_ok": state.serial_ok,
                "serial_port": state.serial_port,
                "active_node": state.active_node,
                "active_ip": state.active_ip,
            }
        )

    # ── Device API proxy — status ─────────────────────────────────────────

    @app.get("/api/status")
    async def _status() -> JSONResponse:
        if state.status:
            return JSONResponse(state.status)
        data = await _proxy_get(_effective_ip(), "/api/status")
        return JSONResponse(data or {})

    # ── Device API proxy — schedule ───────────────────────────────────────

    @app.get("/api/schedule")
    async def _sched_get() -> JSONResponse:
        data = await _proxy_get(_effective_ip(), "/api/schedule")
        return JSONResponse(data or {"schedules": []})

    @app.post("/api/schedule/add")
    async def _sched_add(body: dict) -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/schedule/add", body)
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.post("/api/schedule/remove")
    async def _sched_remove(body: dict) -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/schedule/remove", body)
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.post("/api/schedule/save")
    async def _sched_save() -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/schedule/save", {})
        # Also send via BLE in case HTTP is down
        if _transport:
            await _transport.send_command("SCHED SAVE")
        return PlainTextResponse("OK")

    @app.post("/api/schedule/clear")
    async def _sched_clear() -> PlainTextResponse:
        await _proxy_post(_effective_ip(), "/api/schedule/clear", {})
        if _transport:
            await _transport.send_command("SCHED SAVE")
        return PlainTextResponse("OK")

    @app.get("/api/boards/{board_id}")
    async def _board_get(board_id: str) -> JSONResponse:
        board_file = Path(__file__).parent / "boards" / f"{board_id}.json"
        if not board_file.exists():
            board_file = Path(__file__).parent.parent.parent / "data" / "boards" / f"{board_id}.json"
        
        if board_file.exists():
            try:
                data = json.loads(board_file.read_text())
                return JSONResponse(data)
            except Exception: pass
        return JSONResponse({"error": "Board not found"}, status_code=404)

    @app.get("/api/files")
    @app.get("/api/files/list")
    async def _files_list() -> JSONResponse:
        # Proxy or local? Gateway usually proxies to device, but can show local configs
        files = []
        try:
            for f in (Path(__file__).parent / "configs").glob("*.json"):
                files.append({"name": f.name, "size": f.stat().st_size})
        except Exception: pass
        return JSONResponse({"ok": True, "files": files})

    # ── Fleet Operations ──────────────────────────────────────────────────
    # ── Node Registry / Fleet ─────────────────────────────────────────────

    @app.get("/api/nodes")
    async def _nodes_get() -> JSONResponse:
        nodes = []
        if node_reg:
            for node in node_reg.list():
                nodes.append({
                    "id": node.id,
                    "mac": node.mac,
                    "name": node.name,
                    "type": node.type,
                    "address": node.address,
                    "online": node.online,
                    "active": (node.id == state.active_node or node.address == state.serial_port)
                })
        return JSONResponse({"nodes": nodes})

    @app.post("/api/nodes/{node_id}/connect")
    async def _node_connect(node_id: str) -> JSONResponse:
        if not node_reg:
            return JSONResponse({"ok": False, "error": "No registry"}, status_code=500)
        
        node = node_reg.get(node_id)
        if not node:
            return JSONResponse({"ok": False, "error": f"Node {node_id} not found"}, status_code=404)
        
        state.active_node = node.id
        if node.type == "serial":
            state.serial_port = node.address
            state.active_type = "serial"
        elif node.type in ("wifi", "http"):
            state.active_ip = node.address
            state.active_type = "http"
        
        # Reset and prime transport if needed
        if _transport:
             await _transport.send_command("STATUS", node_id=node_id)

        return JSONResponse({"ok": True, "node": {"id": node.id, "name": node.name}})

    @app.post("/api/fleet/reset")
    async def _fleet_reset(body: dict) -> JSONResponse:
        """Trigger factory reset on targeted or all nodes."""
        node_ids = body.get("node_ids")
        if not node_ids:
            if _transport:
                await _transport.send_command("FACTORY_RESET")
            return JSONResponse({"ok": True, "msg": "Broadcast factory reset sent to all nodes"})
        
        results = []
        if _transport:
            for nid in node_ids:
                ok = await _transport.send_command(f"FORWARD {nid} FACTORY_RESET")
                results.append({"node": nid, "ok": ok})
        return JSONResponse({"ok": True, "results": results})

    # ── OTA Flash — proxied through daemon (port 8001) ───────────────────
    # Daemon is the actor: it knows device IPs, runs pio espota, tracks jobs.
    # Webapp is pure UI — never runs pio directly.
    DAEMON_URL = "http://localhost:8001"

    @app.post("/api/ota/flash")
    async def _ota_flash(request: Request) -> JSONResponse:
        """Proxy OTA flash request to daemon."""
        body = await request.json()
        async with aiohttp.ClientSession() as s:
            try:
                async with s.post(f"{DAEMON_URL}/api/mesh/ota/flash", json=body,
                                  timeout=aiohttp.ClientTimeout(total=5)) as r:
                    return JSONResponse(await r.json(), status_code=r.status)
            except Exception as e:
                return JSONResponse({"ok": False, "error": f"Daemon unavailable: {e}"}, status_code=503)

    @app.get("/api/ota/status/{job_id}")
    async def _ota_status(job_id: str) -> JSONResponse:
        """Proxy OTA status poll to daemon."""
        async with aiohttp.ClientSession() as s:
            try:
                async with s.get(f"{DAEMON_URL}/api/mesh/ota/status/{job_id}",
                                 timeout=aiohttp.ClientTimeout(total=3)) as r:
                    return JSONResponse(await r.json(), status_code=r.status)
            except Exception as e:
                return JSONResponse({"status": "error", "error": f"Daemon unavailable: {e}"}, status_code=503)

    @app.get("/api/ota/fleet")
    async def _ota_fleet() -> JSONResponse:
        """Get online devices from daemon for OTA panel population."""
        async with aiohttp.ClientSession() as s:
            try:
                async with s.get(f"{DAEMON_URL}/api/mesh/ota/fleet",
                                 timeout=aiohttp.ClientTimeout(total=3)) as r:
                    return JSONResponse(await r.json())
            except Exception:
                return JSONResponse({"devices": []})

    # Legacy serial-port flash (kept for backward compatibility)
    @app.post("/api/fleet/flash")
    async def _fleet_flash(env: str, ports: list[str]) -> JSONResponse:
        for p in ports:
            asyncio.create_task(asyncio.create_subprocess_shell(
                f'pio run --target upload --environment {env} --upload-port {p}',
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL
            ))
        return JSONResponse({"ok": True, "msg": f"Flash started for {len(ports)} ports"})

    # ── Daemon config + community — proxy to daemon:8001 ────────────────

    @app.get("/api/daemon/config")
    async def _get_daemon_config() -> JSONResponse:
        async with aiohttp.ClientSession() as s:
            try:
                async with s.get(f"{DAEMON_URL}/api/config", timeout=aiohttp.ClientTimeout(total=3)) as r:
                    return JSONResponse(await r.json())
            except Exception as e:
                return JSONResponse({"error": str(e)}, status_code=503)

    @app.put("/api/daemon/config")
    async def _save_daemon_config(request: Request) -> JSONResponse:
        body = await request.json()
        async with aiohttp.ClientSession() as s:
            try:
                async with s.put(f"{DAEMON_URL}/api/config", json=body, timeout=aiohttp.ClientTimeout(total=5)) as r:
                    return JSONResponse(await r.json(), status_code=r.status)
            except Exception as e:
                return JSONResponse({"ok": False, "error": str(e)}, status_code=503)

    @app.get("/api/community")
    async def _get_community() -> JSONResponse:
        async with aiohttp.ClientSession() as s:
            try:
                async with s.get(f"{DAEMON_URL}/api/community", timeout=aiohttp.ClientTimeout(total=3)) as r:
                    return JSONResponse(await r.json())
            except Exception:
                return JSONResponse({"peers": [], "total": 0, "online": 0})

    # ── SMS / Magic Webhook ──────────────────────────────────────────────

    @app.post("/webhook/twilio", response_class=PlainTextResponse)
    async def _twilio_webhook(Body: str = Form(...), From: str = Form(...)) -> str:
        """Handle incoming SMS from Twilio and route to mesh."""
        print(f"[Magic] SMS from {From}: {Body}")
        
        reply = await _process_magic_sms(Body, From)
        
        if TWILIO:
            twiml = MessagingResponse()
            twiml.message(reply)
            return str(twiml)
        return reply

    async def _process_magic_sms(text: str, sender: str) -> str:
        """Parse natural language SMS into system commands."""
        clean = text.strip().lower()
        
        # 1. Direct command matches
        if "status" in clean:
            s = state.status or {}
            node = state.active_node or "Gateway"
            bat = s.get("bat", "??")
            return f"✨ Magic Status:\nNode: {node}\nBat: {bat}V\nAll systems operational."

        # 2. Simple on/off mapping
        pin = None
        if "led" in clean: pin = 35
        elif "relay 2" in clean or "relay2" in clean: pin = 6
        elif "relay 3" in clean or "relay3" in clean: pin = 7
        
        if pin:
            val = "1" if "on" in clean or "activate" in clean else "0"
            ok = await _transport.send_command(f"GPIO {pin} {val}")
            return f"✅ Magic: Pin {pin} {'set to ' + val if ok else 'failed'}"

        # 3. AI Intent Parsing (The 'Magic' fallback)
        print(f"[Magic] Passing to AI for intent: {text}")
        intent_prompt = f"Available pins: LED=35, RELAY2=6, RELAY3=7. Return only a system command (e.g. GPIO 35 1) for this request: '{text}'"
        cmd = await _ai_gateway.query_ai(intent_prompt)
        
        if cmd and not cmd.startswith("AI_ERR"):
            # Sanitize AI output
            cmd = cmd.strip().upper()
            if cmd.startswith("GPIO") or cmd.startswith("ALL") or cmd.startswith("REBOOT"):
                ok = await _transport.send_command(cmd)
                return f"✅ Magic Brain executed: {cmd}" if ok else f"❌ Command failed: {cmd}"
        
        return "⚠️ Magic didn't recognize that command. Try 'Status' or 'LED on'."

    # ── Transport settings ────────────────────────────────────────────────

    @app.get("/api/settings")
    async def _settings_get() -> JSONResponse:
        return JSONResponse(
            {
                "transport_strategy": state.settings.get(
                    "transport_strategy", "http_first"
                ),
                "options": _TRANSPORT_STRATEGIES,
            }
        )

    @app.post("/api/settings")
    async def _settings_post(body: dict) -> JSONResponse:
        strategy = body.get("transport_strategy", "")
        if strategy not in _TRANSPORT_STRATEGIES:
            return JSONResponse(
                {"ok": False, "error": f"Unknown strategy '{strategy}'"},
                status_code=400,
            )
        state.settings["transport_strategy"] = strategy
        # Reset round-robin counter on strategy change
        if _transport:
            _transport._round_counter = 0
        try:
            SETTINGS_FILE.write_text(
                json.dumps({"transport_strategy": strategy}, indent=2)
            )
        except Exception as e:
            print(f"[settings] Failed to persist: {e}")
        print(f"[settings] Transport strategy → {strategy}")
        return JSONResponse({"ok": True, "transport_strategy": strategy})

    # ── BLE rescan ────────────────────────────────────────────────────────

    @app.post("/api/rescan")
    async def _rescan() -> JSONResponse:
        """Disconnect existing BLE peers, re-scan, reconnect. Runs in background."""
        if not BLEAK:
            return JSONResponse(
                {"ok": False, "error": "BLE not available"}, status_code=503
            )

        async def _do_rescan() -> None:
            for ble in list(_ble_peers):
                try:
                    await ble.disconnect()
                except Exception:
                    pass
            _ble_peers.clear()
            state.ble_ok = state.ble1_ok = state.ble2_ok = False
            state.peer_names = []
            await _ble_scan()  # reuse shared scan helper

        asyncio.create_task(_do_rescan())
        return JSONResponse(
            {"ok": True, "scanning": True, "msg": "Scan started (≈10s)…"}
        )

    @app.get("/api/discover")
    async def _discover() -> JSONResponse:
        """Network discovery: scan subnet for Magic devices. Runs in background."""
        if not AIOHTTP:
            return JSONResponse(
                {"ok": False, "error": "aiohttp not available"}, status_code=503
            )

        async def _scan_subnet() -> None:
            """Scan the local subnet for devices responding to /api/status."""
            found = []
            
            # Dynamically detect local subnet
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.connect(("8.8.8.8", 80))
                local_ip = s.getsockname()[0]
                s.close()
                base_ip = ".".join(local_ip.split(".")[:-1])
                print(f"[discover] Local IP: {local_ip}, scanning {base_ip}.0/24...")
            except Exception:
                # Still fallback, but better than nothing
                base_ip = "172.16.0"
                print(f"[discover] Could not detect local IP, trying {base_ip}.0/24")

            async with aiohttp.ClientSession() as session:
                tasks = []
                for i in range(1, 255):
                    ip = f"{base_ip}.{i}"
                    tasks.append(_probe_device(session, ip))

                results = await asyncio.gather(*tasks, return_exceptions=True)
                found = [r for r in results if r is not None and isinstance(r, dict)]

            # Save discovery results & Register
            for d in found:
                node_reg.add(d["name"], "wifi", d["ip"], online=True)
            
            state.discovered_devices = found
            state.discovery_time = time.time()

        async def _probe_device(session: Any, ip: str) -> Optional[dict]:
            """Probe a single IP and return device info if it's a Magic device."""
            try:
                async with session.get(
                    f"http://{ip}/api/status",
                    timeout=aiohttp.ClientTimeout(total=1.0),
                ) as r:
                    if r.status == 200:
                        data = await r.json()
                        if "myId" in data or "id" in data:  # Magic device
                            return {
                                "ip": ip,
                                "name": data.get("myId") or data.get("id", "Unknown"),
                                "rssi": data.get("rssi"),
                                "bat": data.get("bat"),
                                "uptime": data.get("uptime"),
                            }
            except Exception:
                pass
            return None

        # Start scan in background
        asyncio.create_task(_scan_subnet())
        return JSONResponse(
            {"ok": True, "scanning": True, "msg": "Network scan started (≈15s)…"}
        )

    @app.get("/api/discover/results")
    async def _discover_results() -> JSONResponse:
        """Get results from the last network discovery scan."""
        if not hasattr(state, "discovered_devices"):
            return JSONResponse(
                {
                    "ok": False,
                    "devices": [],
                    "msg": "No scan performed yet. Start with /api/discover",
                }
            )
        return JSONResponse(
            {
                "ok": True,
                "devices": state.discovered_devices or [],
                "timestamp": state.discovery_time if hasattr(state, "discovery_time") else None,
            }
        )

    @app.get("/api/discovery")
    async def _discovery_snapshot() -> JSONResponse:
        """Snapshot of discovered devices (mDNS, BLE, Serial) for automation."""
        # Filter out unnamed or "Unknown" entries to prevent cache clutter (per user request)
        valid_mdns = [
            {"name": d["name"], "ip": d.get("ip") or d.get("address", "")} 
            for d in (state.discovered_devices or [])
            if d.get("name") and d.get("name") != "Unknown"
        ]
        
        return JSONResponse({
            "mdns": valid_mdns,
            "ble": [n for n in (state.peer_names or []) if n and n.strip()],
            "serial": [state.serial_port] if state.serial_port else []
        })

    @app.post("/api/discovery/register")
    async def _discovery_register(body: dict) -> JSONResponse:
        """Register a discovered device into the node registry."""
        name = body.get("name", "").strip()
        type_ = body.get("type", "wifi")
        address = body.get("address", "").strip()
        if not name or not address:
            return JSONResponse({"ok": False, "error": "name and address required"}, status_code=400)
        
        node = node_reg.add(name, type_, address)
        return JSONResponse({"ok": True, "node": node.to_dict()})

    # ── Node registry ─────────────────────────────────────────────────────

    @app.post("/api/nodes/prune")
    async def _nodes_prune() -> JSONResponse:
        count = node_reg.prune()
        return JSONResponse({"ok": True, "pruned": count})

    @app.get("/api/nodes")
    async def _nodes_list() -> JSONResponse:
        return JSONResponse({"ok": True, "nodes": [n.to_dict() for n in node_reg.list()]})

    @app.post("/api/nodes")
    async def _nodes_add(body: dict) -> JSONResponse:
        name = body.get("name", "").strip()
        type_ = body.get("type", "wifi")
        address = body.get("address", "").strip()
        if not name or not address:
            return JSONResponse(
                {"ok": False, "error": "name and address required"}, status_code=400
            )
        if type_ not in ("wifi", "serial", "ble", "lora", "sim"):
            return JSONResponse(
                {"ok": False, "error": f"Unknown type '{type_}'"}, status_code=400
            )
        node = node_reg.add(name, type_, address)
        return JSONResponse({"ok": True, "node": node.to_dict()})

    @app.delete("/api/nodes/{node_id}")
    async def _nodes_delete(node_id: str) -> JSONResponse:
        ok = node_reg.remove(node_id)
        return JSONResponse({"ok": ok})

    @app.get("/api/nodes/{node_id}/proxy/{path:path}")
    async def _node_proxy(node_id: str, path: str) -> Response:
        """Proxy requests to a node by ID, regardless of transport."""
        node = node_reg.get(node_id)
        if not node:
            return PlainTextResponse("Node not found", status_code=404)
        
        if node.type in ("wifi", "lora") and node.address:
            # Proxy to WiFi node
            url = f"http://{{node.address}}/{{path}}"
            try:
                if not AIOHTTP or aiohttp is None:
                    return PlainTextResponse("aiohttp not available", status_code=503)
                
                async with aiohttp.ClientSession() as session:
                    timeout = aiohttp.ClientTimeout(total=3.0)
                    async with session.get(url, timeout=timeout) as r:  # type: ignore
                        content = await r.read()
                        return Response(content=content, media_type=r.content_type)
            except Exception as e:
                return PlainTextResponse(f"Proxy error: {str(e)}", status_code=502)
        
        elif node.type == "serial":
            # For Serial nodes, return a minimal status page since they don't have a web server
            if path == "" or path == "index.html":
                html = f"""
                <html>
                <body style="background:#050505;color:#00ff88;font-family:monospace;padding:20px;">
                    <h3>Node: {node.name} (SERIAL)</h3>
                    <p>Address: {node.address}</p>
                    <hr border="0" style="border-top:1px solid #222">
                    <div id="status">Polling serial status...</div>
                    <script>
                        setInterval(async () => {{
                            try {{
                                // For now, Serial status is pushed via WS, but we can fetch snapshot
                                const r = await fetch('/api/nodes/{node_id}/snapshot');
                                const d = await r.json();
                                document.getElementById('status').innerText = JSON.stringify(d, null, 2);
                            }} catch(e) {{}}
                        }}, 2000);
                    </script>
                </body>
                </html>
                """
                return HTMLResponse(html)
        
        return PlainTextResponse(f"Proxy not supported for {node.type} yet", status_code=501)

    @app.post("/api/nodes/{node_id}/connect")
    async def _nodes_connect(node_id: str) -> JSONResponse:
        nonlocal _serial_link
        node = node_reg.set_active(node_id)
        if not node:
            return JSONResponse(
                {"ok": False, "error": "Node not found"}, status_code=404
            )
        state.active_node = node.name
        state.active_type = node.type
        print(f"[nodes] Active: {node.name} ({node.type}:{node.address})")
        # Route HTTP transport to this node's IP if it has one
        if node.type in ("wifi", "lora") and node.address:
            state.active_ip = node.address
        else:
            state.active_ip = None  # BLE/serial nodes have no HTTP address
        # Wire up the appropriate transport for this node type
        if node.type == "serial" and PYSERIAL:
            if _serial_link:
                try:
                    await _serial_link.disconnect()
                except Exception:
                    pass
            _serial_link = SerialLink(node.address)
            try:
                await _serial_link.connect()
                state.serial_ok = _serial_link.is_connected
                state.serial_port = node.address
            except Exception as e:
                print(f"[serial] Connect failed: {e}")
                state.serial_ok = False
            if _transport:
                _transport._serial = _serial_link
        
        if node.type == "sim" and _sim_bridge:
            if not _sim_bridge._running:
                await _sim_bridge.start()
            state.sim_running = True
        elif _sim_bridge:
            if _sim_bridge._running:
                await _sim_bridge.stop()
            state.sim_running = False
            
        if _poller:
            _poller.trigger()

        return JSONResponse({"ok": True, "node": node.to_dict()})

    @app.get("/api/nodes/{node_id}/snapshot")
    async def _nodes_snapshot(node_id: str) -> JSONResponse:
        """Fetch status + schedule from a specific node (WiFi/LoRa only)."""
        node = node_reg.get(node_id)
        if not node:
            return JSONResponse({"ok": False, "error": "Node not found"}, status_code=404)
        if node.type not in ("wifi", "lora") or not node.address:
            return JSONResponse(
                {"ok": False, "error": f"Node '{node.name}' has no HTTP address (type={node.type})"},
                status_code=400,
            )
        status_data, schedule_data = await asyncio.gather(
            _proxy_get(node.address, "/api/status"),
            _proxy_get(node.address, "/api/schedule"),
        )
        return JSONResponse({
            "ok":        True,
            "node_id":   node_id,
            "node_name": node.name,
            "status":    status_data  or {},
            "schedule":  schedule_data or {"schedules": []},
        })

    # ── Serial ports ──────────────────────────────────────────────────────

    @app.get("/api/serial/ports")
    async def _serial_ports() -> JSONResponse:
        if not PYSERIAL:
            return JSONResponse({"ports": [], "error": "pyserial not installed"})
        
        # Filter for typical ESP32/USB-Serial adapters to avoid dash clutter (e.g. bluetooth/system ports)
        ports = []
        for p in serial.tools.list_ports.comports():
            desc = (p.description or "").lower()
            hwid = (p.hwid or "").lower()
            # Positive tokens for Magic hardware
            if any(t in desc or t in hwid for t in ["esp", "silicon", "cp21", "ch34", "usb-serial", "jlink"]):
                ports.append(p.device)
            # Exclude known-noisy system ports
            elif "standard serial" not in desc and "bluetooth" not in desc and "pci" not in hwid:
                ports.append(p.device)
                
        return JSONResponse({"ports": sorted(ports)})

    # ── Test sequences ────────────────────────────────────────────────────

    @app.get("/api/sequences")
    async def _seq_list() -> JSONResponse:
        return JSONResponse([s.to_dict() for s in seq_reg.list()])

    @app.post("/api/sequences")
    async def _seq_save(body: dict) -> JSONResponse:
        name = body.get("name", "").strip()
        if not name:
            return JSONResponse(
                {"ok": False, "error": "name required"}, status_code=400
            )
        tasks = [TaskSpec.from_dict(t) for t in body.get("tasks", [])]
        seq_reg.save(Sequence(name=name, tasks=tasks))
        return JSONResponse({"ok": True, "name": name, "count": len(tasks)})

    @app.delete("/api/sequences/{name}")
    async def _seq_delete(name: str) -> JSONResponse:
        ok = seq_reg.delete(name)
        return JSONResponse({"ok": ok})

    @app.post("/api/sequences/{name}/apply")
    async def _seq_apply(name: str) -> JSONResponse:
        seq = seq_reg.get(name)
        if not seq:
            return JSONResponse(
                {"ok": False, "error": "Sequence not found"}, status_code=404
            )
        if not _transport:
            return JSONResponse({"ok": False, "error": "No transport"}, status_code=503)
        results = []
        for t in seq.tasks:
            dur = f" {t.duration}" if t.duration else ""
            cmd = f"SCHED ADD {t.name} {t.type} {t.pin} {t.interval}{dur}"
            ok = await _transport.send_command(cmd)
            results.append({"task": t.name, "ok": ok})
            await asyncio.sleep(0.2)
        # Persist to firmware flash
        await _transport.send_command("SCHED SAVE")
        return JSONResponse({"ok": True, "results": results})

    @app.post("/api/sequences/{name}/apply-multi")
    async def _seq_apply_multi(name: str, body: dict) -> JSONResponse:
        """Apply a sequence to multiple WiFi/LoRa nodes in parallel."""
        seq = seq_reg.get(name)
        if not seq:
            return JSONResponse({"ok": False, "error": "Sequence not found"}, status_code=404)
        node_ids: list = body.get("node_ids", [])
        if not node_ids:
            return JSONResponse({"ok": False, "error": "node_ids required"}, status_code=400)

        per_node_results = []
        for nid in node_ids:
            node = node_reg.get(nid)
            if not node:
                per_node_results.append({"node_id": nid, "ok": False, "error": "not found"})
                continue
            if node.type not in ("wifi", "lora") or not node.address:
                per_node_results.append({
                    "node_id": nid, "node_name": node.name,
                    "ok": False, "error": "skipped — no HTTP address",
                })
                continue
            task_results = []
            for t in seq.tasks:
                dur = f" {t.duration}" if t.duration else ""
                cmd = f"SCHED ADD {t.name} {t.type} {t.pin} {t.interval}{dur}"
                ok  = await _send_cmd_to_ip(node.address, cmd)
                task_results.append({"task": t.name, "ok": ok})
                await asyncio.sleep(0.15)
            await _send_cmd_to_ip(node.address, "SCHED SAVE")
            ok_count = sum(1 for x in task_results if x["ok"])
            per_node_results.append({
                "node_id":     nid,
                "node_name":   node.name,
                "address":     node.address,
                "tasks_ok":    ok_count,
                "tasks_total": len(task_results),
                "ok":          ok_count == len(task_results),
                "results":     task_results,
            })
        return JSONResponse({"ok": True, "sequence": name, "nodes": per_node_results})

    # ── Config files ──────────────────────────────────────────────────────

    @app.get("/api/files")
    async def _files_list() -> JSONResponse:
        files = []
        for p in sorted(CONFIGS_DIR.iterdir()):
            if p.suffix.lower() in (".json", ".csv") and p.is_file():
                files.append({"name": p.name, "size": p.stat().st_size})
        return JSONResponse({"files": files})

    @app.get("/api/files/{filename}")
    async def _files_get(filename: str) -> PlainTextResponse:
        p = CONFIGS_DIR / filename
        if not p.exists() or p.suffix.lower() not in (".json", ".csv"):
            return PlainTextResponse("Not found", status_code=404)
        async with aiofiles.open(p, "r") as f:
            content = await f.read()
        return PlainTextResponse(content)

    @app.post("/api/files/{filename}")
    async def _files_save(filename: str, body: dict) -> JSONResponse:
        if not filename.endswith((".json", ".csv")):
            return JSONResponse(
                {"ok": False, "error": "Only .json and .csv allowed"}, status_code=400
            )
        content = body.get("content", "")
        p = CONFIGS_DIR / filename
        async with aiofiles.open(p, "w") as f:
            await f.write(content)
        return JSONResponse({"ok": True, "name": filename, "size": len(content)})

    @app.delete("/api/files/{filename}")
    async def _files_delete(filename: str) -> JSONResponse:
        p = CONFIGS_DIR / filename
        if not p.exists():
            return JSONResponse({"ok": False, "error": "Not found"}, status_code=404)
        p.unlink()
        return JSONResponse({"ok": True})

    # ── Device Side Config/Files ──────────────────────────────────────────

    @app.get("/api/device/config")
    async def _device_config_get() -> JSONResponse:
        content = await _proxy_get(_effective_ip(), "/api/config")
        return JSONResponse(content or {"error": "Device unreachable"})

    @app.post("/api/device/config/apply")
    async def _device_config_apply(body: dict) -> PlainTextResponse:
        # body is the full JSON config
        ip = _effective_ip()

        # Try HTTP first (device proxy)
        ok = await _proxy_post(ip, "/api/config/apply", body)
        if ok:
            return PlainTextResponse("OK", status_code=200)

        # HTTP failed — provide helpful feedback about available transports
        transports_available = []
        if _transport:
            if state.ble_ok or (len(_transport._peers) > 0):
                transports_available.append("BLE")
            if state.serial_ok:
                transports_available.append("Serial")
            if state.espnow_ok:
                transports_available.append("ESP-NOW")

        msg = f"Device not reachable via HTTP at {ip}."
        if transports_available:
            msg += f" Available transports: {', '.join(transports_available)}. Use command console to apply config."
        else:
            msg += " No alternative transports available. Make sure device is powered and in range."

        return PlainTextResponse(msg, status_code=502)

    @app.get("/api/device/files")
    async def _device_files_list() -> JSONResponse:
        content = await _proxy_get(_effective_ip(), "/api/files/list")
        return JSONResponse(content or {"files": []})

    @app.post("/api/pins/enable")
    async def _pin_enable(body: dict) -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/pins/enable", body)
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.post("/api/pins/name")
    async def _pin_name(body: dict) -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/pins/name", body)
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.post("/api/transport/mode")
    async def _transport_mode(body: dict) -> PlainTextResponse:
        ok = await _proxy_post(_effective_ip(), "/api/transport/mode", body)
        return PlainTextResponse("OK" if ok else "ERR", status_code=200 if ok else 502)

    @app.get("/api/registry")
    async def _registry() -> JSONResponse:
        data = await _proxy_get(_effective_ip(), "/api/registry")
        return JSONResponse(data or {})

    @app.post("/api/simulator/toggle")
    async def _sim_toggle() -> JSONResponse:
        if not _sim_bridge:
            return JSONResponse({"ok": False, "error": "Simulator not available"}, status_code=503)
        if _sim_bridge._running:
            await _sim_bridge.stop()
            state.sim_running = False
        else:
            await _sim_bridge.start()
            state.sim_running = True
        return JSONResponse({"ok": True, "active": state.sim_running})

    @app.get("/api/protodev/plugins")
    async def _proto_plugins() -> JSONResponse:
        if state.sim_running and _sim_bridge:
            return JSONResponse({"plugins": _sim_bridge.get_status().get("plugins", [])})
        data = await _proxy_get(_effective_ip(), "/api/plugins")
        return JSONResponse(data or {"plugins": []})

    @app.get("/api/protodev/hal")
    async def _proto_hal() -> JSONResponse:
        if state.sim_running and _sim_bridge:
            return JSONResponse({"pins": _sim_bridge.get_status().get("pins", [])})
        data = await _proxy_get(_effective_ip(), "/api/hardware/map")
        return JSONResponse(data or {"pins": []})

    @app.post("/api/protodev/cmd")
    async def _proto_cmd(body: dict) -> JSONResponse:
        cmd = body.get("cmd", "").strip()
        if state.sim_running and _sim_bridge:
            ok = await _sim_bridge.send_command(cmd)
            return JSONResponse({"ok": ok, "resp": "Sim CMD sent" if ok else "Sim CMD Failed"})
        ok = await _proxy_post(_effective_ip(), "/api/cmd", body)
        return JSONResponse({"ok": ok, "resp": "Hardware CMD sent" if ok else "Hardware CMD Failed"})

    @app.get("/api/device/files/read")
    async def _device_file_read(path: str) -> Response:
        if not device_ip:
            return PlainTextResponse("No device IP", status_code=503)
        if not AIOHTTP or aiohttp is None:
            return PlainTextResponse("aiohttp not available", status_code=503)
        try:
            async with aiohttp.ClientSession() as s:
                async with s.get(f"http://{device_ip}/api/files/read?path={path}") as r:
                    content = await r.read()
                    return Response(content=content, media_type=r.content_type, status_code=r.status)
        except Exception as e:
            return PlainTextResponse(str(e), status_code=500)

    # ── Daemon proxy endpoints ────────────────────────────────────────────────

    @app.get("/api/daemon/nodes")
    async def get_daemon_nodes():
        """Fetch node list from daemon — used by WebSocket broadcast."""
        return await _daemon_client.list_nodes()

    @app.get("/api/daemon/health")
    async def get_daemon_health():
        """Check if daemon is reachable."""
        is_healthy = await _daemon_client.health()
        return {"daemon_connected": is_healthy}

    @app.get("/api/daemon/carriers")
    async def get_daemon_carriers():
        """List available carrier board profiles from daemon."""
        return await _daemon_client.list_carriers()

    @app.post("/api/daemon/provision")
    async def post_daemon_provision(body: dict):
        """Provision a device via daemon — sets carrier, features, identity, triggers reboot."""
        device_id = body.get("device_id")
        carrier = body.get("carrier")
        if not device_id or not carrier:
            return JSONResponse(
                status_code=400,
                content={"error": "device_id and carrier are required"},
            )
        result = await _daemon_client.provision_device(
            device_id=device_id,
            carrier=carrier,
            features=body.get("features"),
            identity=body.get("identity"),
            reboot=body.get("reboot", True),
        )
        status_code = 200 if result.get("status") == "ok" else 500
        return JSONResponse(status_code=status_code, content=result)

    # ── Hybrid Model Proxy API ────────────────────────────────────────────────

    @app.get("/api/proxy/status")
    async def _proxy_status() -> JSONResponse:
        """Get hybrid proxy status, health checks, and metrics."""
        try:
            # Try to get proxy status from HTTP endpoint
            if not AIOHTTP:
                return JSONResponse({
                    "running": False,
                    "error": "aiohttp not available",
                    "ollama_healthy": False,
                    "openrouter_healthy": False,
                    "total_requests": 0,
                    "recent_requests": []
                })

            async with aiohttp.ClientSession() as session:
                try:
                    async with session.get("http://localhost:5555/status", timeout=aiohttp.ClientTimeout(total=2)) as r:
                        if r.status == 200:
                            data = await r.json()
                            return JSONResponse(data)
                except Exception:
                    pass

            # Proxy not running - return stub
            return JSONResponse({
                "running": False,
                "ollama_healthy": False,
                "openrouter_healthy": False,
                "total_requests": 0,
                "ollama_requests": 0,
                "openrouter_requests": 0,
                "ollama_cost": 0.0,
                "openrouter_cost": 0.0,
                "total_cost": 0.0,
                "uptime": "—",
                "recent_requests": []
            })
        except Exception as e:
            print(f"[proxy] Status check error: {e}")
            return JSONResponse({
                "running": False,
                "error": str(e),
                "total_requests": 0,
                "recent_requests": []
            })

    @app.post("/api/proxy/start")
    async def _proxy_start() -> JSONResponse:
        """Start the hybrid proxy server."""
        import subprocess
        import platform
        try:
            proxy_script = Path(__file__).parent.parent / "hybrid_model_proxy.py"
            if not proxy_script.exists():
                return JSONResponse({
                    "ok": False,
                    "error": "hybrid_model_proxy.py not found"
                }, status_code=404)

            # Start proxy in background
            if platform.system() == "Windows":
                subprocess.Popen(["python", str(proxy_script)], creationflags=subprocess.CREATE_NEW_CONSOLE)
            else:
                subprocess.Popen(["python3", str(proxy_script)])

            return JSONResponse({"ok": True, "msg": "Proxy starting..."})
        except Exception as e:
            return JSONResponse({"ok": False, "error": str(e)}, status_code=500)

    @app.post("/api/proxy/stop")
    async def _proxy_stop() -> JSONResponse:
        """Stop the hybrid proxy server."""
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post("http://localhost:5555/shutdown", timeout=aiohttp.ClientTimeout(total=2)) as r:
                    if r.status == 200:
                        return JSONResponse({"ok": True, "msg": "Proxy stopping..."})
        except Exception:
            pass

        # Try via system call
        import subprocess
        import platform
        try:
            if platform.system() == "Windows":
                subprocess.run(["taskkill", "/FI", "WINDOWTITLE eq Hybrid Model Proxy*", "/T", "/F"], check=False)
            else:
                subprocess.run(["pkill", "-f", "hybrid_model_proxy.py"], check=False)
            return JSONResponse({"ok": True, "msg": "Proxy stopped"})
        except Exception as e:
            return JSONResponse({"ok": False, "error": str(e)}, status_code=500)

    @app.post("/api/proxy/config")
    async def _proxy_config(body: dict) -> JSONResponse:
        """Save proxy configuration."""
        try:
            config_file = Path.home() / ".claude" / "hybrid_proxy_config.json"
            config_file.parent.mkdir(parents=True, exist_ok=True)
            config_file.write_text(json.dumps(body, indent=2))
            return JSONResponse({"ok": True, "msg": "Configuration saved"})
        except Exception as e:
            return JSONResponse({"ok": False, "error": str(e)}, status_code=500)

    @app.post("/api/proxy/test-openrouter")
    async def _proxy_test_openrouter(body: dict = None) -> JSONResponse:
        """Test OpenRouter API connectivity."""
        if not AIOHTTP:
            return JSONResponse({"success": False, "error": "aiohttp not available"})

        key = (body or {}).get("openrouter_key", "") if body else ""
        if not key:
            return JSONResponse({"success": False, "error": "API key required"})

        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(
                    "https://openrouter.ai/api/v1/models",
                    headers={"Authorization": f"Bearer {key}"},
                    timeout=aiohttp.ClientTimeout(total=5)
                ) as r:
                    if r.status == 200:
                        return JSONResponse({"success": True})
                    else:
                        return JSONResponse({
                            "success": False,
                            "error": f"HTTP {r.status}"
                        })
        except Exception as e:
            return JSONResponse({"success": False, "error": str(e)})

    return app


# ════════════════════════════════════════════════════════════════════════════
# 7. CLI entry point
# ════════════════════════════════════════════════════════════════════════════


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Magic PC Control Webapp",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--device",
        default="HT-LoRa",
        metavar="PREFIX",
        help="BLE name prefix for peer A  (default: HT-LoRa)",
    )
    parser.add_argument(
        "--device2",
        default=None,
        metavar="PREFIX2",
        help="BLE name prefix for peer B  (optional; omit for single-device mode)",
    )
    parser.add_argument(
        "--ip",
        default=None,
        metavar="ADDRESS",
        help="Device IP for HTTP transport  (optional)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        metavar="PORT",
        help="Local port to serve on  (default: $PORT env var, then 8000)",
    )
    parser.add_argument(
        "--no-ble",
        action="store_true",
        help="Disable BLE scanning entirely (HTTP-only mode)",
    )
    args = parser.parse_args()

    # Honour PORT env var injected by preview tools / CI; CLI flag overrides it.
    port = args.port or int(os.environ.get("PORT", 8000))

    # Resolve DNS hostname to IP if provided
    device_ip = args.ip
    if device_ip and "." not in device_ip:  # Likely a hostname (no dots)
        try:
            import socket
            resolved_ip = socket.gethostbyname(device_ip)
            print(f"[DNS] Resolved '{device_ip}' → {resolved_ip}")
            device_ip = resolved_ip
        except Exception as e:
            print(f"[DNS] Failed to resolve '{device_ip}': {e}")
            print(f"[DNS] Will retry during operation...")

    print("Magic PC Control Webapp")
    print(f"  Peer A     : {args.device}")
    print(f"  Peer B     : {args.device2 or '(single-device mode)'}")
    print(f"  Device IP  : {device_ip or 'not set (BLE only)'}")
    print(
        f"  BLE        : {'disabled (--no-ble)' if args.no_ble else 'enabled (background scan)'}"
    )
    host = os.environ.get("HOST", "0.0.0.0")
    print(f"  Serving at : http://{host}:{port}")
    print()

    app = build_app(
        device_name=args.device,
        device_ip=device_ip,
        device_name2=args.device2,
        no_ble=args.no_ble,
    )
    uvicorn.run(app, host=host, port=port, log_level="warning")


if __name__ == "__main__":
    main()
