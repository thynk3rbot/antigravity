# PC Daemon + Webapp Separation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Separate device transport logic (PC Daemon) from UI logic (Webapp). Enable multiple clients (phone, webapp, cloud) to control Magic swarm via unified daemon.

**Architecture:** PC Daemon runs as a background service and exposes a REST API + WebSocket. All device communication (Serial, HTTP, BLE, MQTT) is routed through the daemon. Webapp and other clients talk *only* to the daemon, never directly to devices.

**Tech Stack:** Python (FastAPI, SQLAlchemy), SQLite, aiohttp, asyncio

---

## Phase 1: Daemon Foundation

### Task 1: Create Daemon Project Structure

**Files:**
- Create: `tools/daemon/__init__.py`
- Create: `tools/daemon/daemon.py`
- Create: `tools/daemon/config.py`
- Create: `tools/daemon/models.py`
- Create: `tools/daemon/api.py`
- Create: `tools/daemon/transport.py`
- Create: `tools/daemon/persistence.py`
- Create: `tools/daemon/README.md`

**Step 1: Create daemon directory and init file**

```bash
mkdir -p tools/daemon
touch tools/daemon/__init__.py
```

**Step 2: Create README documenting daemon purpose**

File: `tools/daemon/README.md`

```markdown
# Magic PC Daemon

Central transport hub for Magic device control.

## Features
- Multi-protocol transport (Serial, HTTP, BLE, LoRa, MQTT)
- Message persistence (SQLite)
- Device discovery & topology tracking
- REST API for clients (webapp, mobile, cloud)
- WebSocket for real-time events

## Architecture
- `daemon.py` — Main daemon service
- `api.py` — FastAPI REST/WebSocket endpoints
- `transport.py` — Transport abstraction layer
- `persistence.py` — SQLite message queue & state
- `models.py` — Data structures (Node, Message, Transport)
- `config.py` — Configuration management

## Running
```bash
python tools/daemon/daemon.py --config daemon.config.json
```
```

**Step 3: Commit**

```bash
git add tools/daemon/
git commit -m "chore: create daemon project skeleton"
```

---

### Task 2: Implement Daemon Data Models

**Files:**
- Create: `tools/daemon/models.py`

**Step 1: Write test file for models**

File: `tests/daemon/test_models.py`

```python
import pytest
from tools.daemon.models import Node, Message, Transport

def test_node_creation():
    node = Node(id="node1", name="TestNode", type="wifi", address="192.168.1.50")
    assert node.id == "node1"
    assert node.name == "TestNode"
    assert node.type == "wifi"
    assert node.address == "192.168.1.50"
    assert node.online == True
    assert node.last_seen is not None

def test_message_creation():
    msg = Message(src="node1", dest="node2", command="GPIO 5 HIGH", status="QUEUED")
    assert msg.src == "node1"
    assert msg.dest == "node2"
    assert msg.command == "GPIO 5 HIGH"
    assert msg.status == "QUEUED"

def test_transport_enum():
    assert Transport.SERIAL.value == "serial"
    assert Transport.HTTP.value == "http"
    assert Transport.BLE.value == "ble"
    assert Transport.LORA.value == "lora"
    assert Transport.MQTT.value == "mqtt"
```

**Step 2: Create models.py with data structures**

File: `tools/daemon/models.py`

```python
from enum import Enum
from dataclasses import dataclass, field
from typing import Optional
import time
import uuid

class Transport(Enum):
    """Available transport protocols"""
    SERIAL = "serial"
    HTTP = "http"
    BLE = "ble"
    LORA = "lora"
    MQTT = "mqtt"

class MessageStatus(Enum):
    """Message delivery status"""
    QUEUED = "queued"
    SENT = "sent"
    ACKED = "acked"
    FAILED = "failed"
    DELIVERED = "delivered"

@dataclass
class Node:
    """Device node in the swarm"""
    id: str
    name: str
    type: str  # wifi, serial, ble, lora
    address: str
    hardware: str = "Unknown"
    mac: str = "Unknown"
    online: bool = True
    last_seen: float = field(default_factory=time.time)
    preferred_transport: Optional[Transport] = None

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "address": self.address,
            "hardware": self.hardware,
            "mac": self.mac,
            "online": self.online,
            "last_seen": self.last_seen,
            "preferred_transport": self.preferred_transport.value if self.preferred_transport else None,
        }

@dataclass
class Message:
    """Command message in the queue"""
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    src: str = "daemon"
    dest: str = ""
    command: str = ""
    status: MessageStatus = MessageStatus.QUEUED
    created_at: float = field(default_factory=time.time)
    sent_at: Optional[float] = None
    acked_at: Optional[float] = None
    transport_used: Optional[Transport] = None
    retry_count: int = 0

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "src": self.src,
            "dest": self.dest,
            "command": self.command,
            "status": self.status.value,
            "created_at": self.created_at,
            "sent_at": self.sent_at,
            "acked_at": self.acked_at,
            "transport_used": self.transport_used.value if self.transport_used else None,
            "retry_count": self.retry_count,
        }
```

**Step 3: Run tests**

```bash
pytest tests/daemon/test_models.py -v
```

Expected: PASS (all 3 tests)

**Step 4: Commit**

```bash
git add tools/daemon/models.py tests/daemon/test_models.py
git commit -m "feat: implement daemon data models (Node, Message, Transport)"
```

---

### Task 3: Implement SQLite Persistence Layer

**Files:**
- Create: `tools/daemon/persistence.py`

**Step 1: Write persistence tests**

File: `tests/daemon/test_persistence.py`

```python
import pytest
import tempfile
from pathlib import Path
from tools.daemon.persistence import MessageQueue
from tools.daemon.models import Message, MessageStatus, Transport

def test_message_queue_save_and_load():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg = Message(dest="node1", command="GPIO 5 HIGH")
        queue.save_message(msg)

        loaded = queue.get_message(msg.id)
        assert loaded.id == msg.id
        assert loaded.dest == "node1"
        assert loaded.command == "GPIO 5 HIGH"

def test_message_queue_list_by_status():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg1 = Message(dest="node1", command="CMD1", status=MessageStatus.QUEUED)
        msg2 = Message(dest="node2", command="CMD2", status=MessageStatus.SENT)

        queue.save_message(msg1)
        queue.save_message(msg2)

        queued = queue.list_messages(status=MessageStatus.QUEUED)
        assert len(queued) == 1
        assert queued[0].command == "CMD1"

def test_message_queue_update_status():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg = Message(dest="node1", command="CMD")
        queue.save_message(msg)

        queue.update_status(msg.id, MessageStatus.SENT)
        loaded = queue.get_message(msg.id)
        assert loaded.status == MessageStatus.SENT
```

**Step 2: Implement persistence layer**

File: `tools/daemon/persistence.py`

```python
import sqlite3
from pathlib import Path
from typing import Optional, List
from datetime import datetime
from tools.daemon.models import Message, MessageStatus, Transport
import json

class MessageQueue:
    """SQLite-backed message queue for daemon persistence"""

    def __init__(self, db_path: Path):
        self.db_path = db_path
        self._init_db()

    def _init_db(self):
        """Initialize SQLite schema"""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS messages (
                    id TEXT PRIMARY KEY,
                    src TEXT,
                    dest TEXT,
                    command TEXT,
                    status TEXT,
                    created_at REAL,
                    sent_at REAL,
                    acked_at REAL,
                    transport_used TEXT,
                    retry_count INTEGER
                )
            """)
            cursor.execute("""
                CREATE INDEX IF NOT EXISTS idx_status ON messages(status)
            """)
            cursor.execute("""
                CREATE INDEX IF NOT EXISTS idx_dest ON messages(dest)
            """)
            conn.commit()

    def save_message(self, msg: Message) -> None:
        """Save or update message in queue"""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT OR REPLACE INTO messages
                (id, src, dest, command, status, created_at, sent_at, acked_at, transport_used, retry_count)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                msg.id, msg.src, msg.dest, msg.command,
                msg.status.value, msg.created_at, msg.sent_at, msg.acked_at,
                msg.transport_used.value if msg.transport_used else None,
                msg.retry_count
            ))
            conn.commit()

    def get_message(self, msg_id: str) -> Optional[Message]:
        """Retrieve message by ID"""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM messages WHERE id = ?", (msg_id,))
            row = cursor.fetchone()
            if not row:
                return None
            return self._row_to_message(row)

    def list_messages(self, status: Optional[MessageStatus] = None, dest: Optional[str] = None, limit: int = 100) -> List[Message]:
        """List messages by filter"""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            query = "SELECT * FROM messages WHERE 1=1"
            params = []

            if status:
                query += " AND status = ?"
                params.append(status.value)

            if dest:
                query += " AND dest = ?"
                params.append(dest)

            query += " ORDER BY created_at DESC LIMIT ?"
            params.append(limit)

            cursor.execute(query, params)
            rows = cursor.fetchall()
            return [self._row_to_message(row) for row in rows]

    def update_status(self, msg_id: str, status: MessageStatus) -> None:
        """Update message status"""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.cursor()
            cursor.execute("UPDATE messages SET status = ? WHERE id = ?", (status.value, msg_id))
            conn.commit()

    def _row_to_message(self, row) -> Message:
        """Convert SQLite row to Message object"""
        return Message(
            id=row[0],
            src=row[1],
            dest=row[2],
            command=row[3],
            status=MessageStatus(row[4]),
            created_at=row[5],
            sent_at=row[6],
            acked_at=row[7],
            transport_used=Transport(row[8]) if row[8] else None,
            retry_count=row[9]
        )
```

**Step 3: Run tests**

```bash
pytest tests/daemon/test_persistence.py -v
```

Expected: PASS (all 3 tests)

**Step 4: Commit**

```bash
git add tools/daemon/persistence.py tests/daemon/test_persistence.py
git commit -m "feat: implement SQLite message queue persistence"
```

---

### Task 4: Implement Transport Abstraction Layer

**Files:**
- Create: `tools/daemon/transport.py`

**Step 1: Write transport tests**

File: `tests/daemon/test_transport.py`

```python
import pytest
from unittest.mock import AsyncMock, patch
from tools.daemon.transport import TransportManager
from tools.daemon.models import Transport, Node

@pytest.mark.asyncio
async def test_transport_manager_select_http():
    """Test HTTP transport selection for WiFi node"""
    manager = TransportManager()

    node = Node(id="node1", name="WiFi Node", type="wifi", address="192.168.1.50")

    # Mock HTTP availability
    with patch.object(manager, '_probe_http', new_callable=AsyncMock) as mock_http:
        mock_http.return_value = True

        selected = await manager.select_transport(node)
        assert selected == Transport.HTTP

@pytest.mark.asyncio
async def test_transport_manager_fallback_to_ble():
    """Test fallback from HTTP to BLE when HTTP fails"""
    manager = TransportManager()

    node = Node(id="node1", name="WiFi Node", type="wifi", address="192.168.1.50")

    # Mock HTTP fail, BLE success
    with patch.object(manager, '_probe_http', new_callable=AsyncMock) as mock_http:
        with patch.object(manager, '_probe_ble', new_callable=AsyncMock) as mock_ble:
            mock_http.return_value = False
            mock_ble.return_value = True

            selected = await manager.select_transport(node)
            assert selected == Transport.BLE
```

**Step 2: Implement transport manager**

File: `tools/daemon/transport.py`

```python
import asyncio
from typing import Optional, Dict
from tools.daemon.models import Transport, Node
import logging

logger = logging.getLogger(__name__)

class TransportManager:
    """Manages device communication across multiple transports"""

    def __init__(self):
        self.transports: Dict[Transport, "TransportHandler"] = {}
        self._http_handler = HTTPTransport()
        self._ble_handler = BLETransport()
        self._serial_handler = SerialTransport()

    async def select_transport(self, node: Node) -> Transport:
        """
        Intelligently select best transport for node.
        Tries preferred transport first, then falls back to others.
        """
        # If node has preferred transport and it's available, use it
        if node.preferred_transport:
            if await self._probe_transport(node, node.preferred_transport):
                return node.preferred_transport

        # Try transports in order of preference for this node type
        if node.type == "wifi":
            transports = [Transport.HTTP, Transport.BLE]
        elif node.type == "ble":
            transports = [Transport.BLE, Transport.HTTP]
        elif node.type == "serial":
            transports = [Transport.SERIAL]
        else:
            transports = [Transport.HTTP, Transport.BLE, Transport.SERIAL]

        for transport in transports:
            if await self._probe_transport(node, transport):
                logger.info(f"Selected {transport.value} for {node.name}")
                return transport

        # No transport available
        raise RuntimeError(f"No available transport for {node.name}")

    async def _probe_transport(self, node: Node, transport: Transport) -> bool:
        """Test if transport is available for node"""
        try:
            if transport == Transport.HTTP:
                return await self._probe_http(node)
            elif transport == Transport.BLE:
                return await self._probe_ble(node)
            elif transport == Transport.SERIAL:
                return await self._probe_serial(node)
            return False
        except Exception as e:
            logger.debug(f"Probe {transport.value} failed: {e}")
            return False

    async def _probe_http(self, node: Node) -> bool:
        """Check if HTTP is reachable"""
        return self._http_handler.is_reachable(node)

    async def _probe_ble(self, node: Node) -> bool:
        """Check if BLE is reachable"""
        return self._ble_handler.is_reachable(node)

    async def _probe_serial(self, node: Node) -> bool:
        """Check if Serial is reachable"""
        return self._serial_handler.is_reachable(node)

    async def send_command(self, node: Node, command: str) -> bool:
        """Send command to node via best available transport"""
        transport = await self.select_transport(node)

        if transport == Transport.HTTP:
            return await self._http_handler.send(node, command)
        elif transport == Transport.BLE:
            return await self._ble_handler.send(node, command)
        elif transport == Transport.SERIAL:
            return await self._serial_handler.send(node, command)

        return False

class TransportHandler:
    """Base class for transport handlers"""

    def is_reachable(self, node: Node) -> bool:
        raise NotImplementedError

    async def send(self, node: Node, command: str) -> bool:
        raise NotImplementedError

class HTTPTransport(TransportHandler):
    def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual HTTP probe
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual HTTP send
        return True

class BLETransport(TransportHandler):
    def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual BLE probe
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual BLE send
        return True

class SerialTransport(TransportHandler):
    def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual Serial probe
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual Serial send
        return True
```

**Step 3: Run tests**

```bash
pytest tests/daemon/test_transport.py -v
```

Expected: PASS (all 2 tests)

**Step 4: Commit**

```bash
git add tools/daemon/transport.py tests/daemon/test_transport.py
git commit -m "feat: implement transport abstraction layer with fallback logic"
```

---

### Task 5: Implement FastAPI REST API

**Files:**
- Create: `tools/daemon/api.py`

**Step 1: Implement basic API**

File: `tools/daemon/api.py`

```python
from fastapi import FastAPI, WebSocket, HTTPException
from fastapi.responses import JSONResponse
from typing import List, Optional
from tools.daemon.models import Node, Message, MessageStatus
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager
import asyncio
import logging

logger = logging.getLogger(__name__)

def create_api_app(message_queue: MessageQueue, transport_manager: TransportManager) -> FastAPI:
    """Create FastAPI application with daemon endpoints"""

    app = FastAPI(title="Magic Daemon API")

    # In-memory node registry (TODO: persist to DB)
    nodes: List[Node] = []

    # ─────────────────────────────────────────────────────────
    # Node Management Endpoints
    # ─────────────────────────────────────────────────────────

    @app.post("/api/nodes")
    async def add_node(node_data: dict):
        """Register a new node"""
        node = Node(
            id=node_data["id"],
            name=node_data["name"],
            type=node_data["type"],
            address=node_data["address"]
        )
        nodes.append(node)
        logger.info(f"Registered node: {node.name}")
        return node.to_dict()

    @app.get("/api/nodes")
    async def list_nodes():
        """List all known nodes"""
        return [n.to_dict() for n in nodes]

    @app.get("/api/nodes/{node_id}")
    async def get_node(node_id: str):
        """Get specific node by ID"""
        node = next((n for n in nodes if n.id == node_id), None)
        if not node:
            raise HTTPException(status_code=404, detail="Node not found")
        return node.to_dict()

    # ─────────────────────────────────────────────────────────
    # Command/Message Endpoints
    # ─────────────────────────────────────────────────────────

    @app.post("/api/command")
    async def send_command(command_data: dict):
        """
        Send command to device.

        Payload:
        {
            "dest": "node_id",
            "command": "GPIO 5 HIGH",
            "priority": "normal"  # or "high", "low"
        }
        """
        dest = command_data["dest"]
        command_str = command_data["command"]

        # Find destination node
        node = next((n for n in nodes if n.id == dest), None)
        if not node:
            raise HTTPException(status_code=404, detail=f"Node {dest} not found")

        # Create message
        msg = Message(dest=dest, command=command_str)
        message_queue.save_message(msg)

        # Send via transport manager
        try:
            success = await transport_manager.send_command(node, command_str)
            if success:
                message_queue.update_status(msg.id, MessageStatus.SENT)
                return {"id": msg.id, "status": "SENT"}
            else:
                message_queue.update_status(msg.id, MessageStatus.FAILED)
                return {"id": msg.id, "status": "FAILED"}
        except Exception as e:
            logger.error(f"Command send failed: {e}")
            message_queue.update_status(msg.id, MessageStatus.FAILED)
            raise HTTPException(status_code=500, detail=str(e))

    @app.get("/api/messages")
    async def list_messages(status: Optional[str] = None, dest: Optional[str] = None):
        """List messages in queue"""
        msg_status = None
        if status:
            msg_status = MessageStatus(status)

        messages = message_queue.list_messages(status=msg_status, dest=dest, limit=100)
        return [m.to_dict() for m in messages]

    @app.get("/api/messages/{msg_id}")
    async def get_message(msg_id: str):
        """Get message status by ID"""
        msg = message_queue.get_message(msg_id)
        if not msg:
            raise HTTPException(status_code=404, detail="Message not found")
        return msg.to_dict()

    # ─────────────────────────────────────────────────────────
    # Health Endpoint
    # ─────────────────────────────────────────────────────────

    @app.get("/health")
    async def health_check():
        """Daemon health status"""
        return {
            "status": "ok",
            "nodes_count": len(nodes),
            "node_ids": [n.id for n in nodes]
        }

    return app
```

**Step 2: Create main daemon service**

File: `tools/daemon/daemon.py`

```python
import asyncio
import logging
from pathlib import Path
import json
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager
from tools.daemon.api import create_api_app
import uvicorn

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class MagicDaemon:
    """Main daemon service"""

    def __init__(self, config_path: Path):
        self.config_path = config_path
        self.config = self._load_config()

        # Initialize components
        self.message_queue = MessageQueue(Path(self.config["db_path"]))
        self.transport_manager = TransportManager()
        self.api_app = create_api_app(self.message_queue, self.transport_manager)

    def _load_config(self) -> dict:
        """Load daemon configuration"""
        if self.config_path.exists():
            return json.loads(self.config_path.read_text())

        # Default config
        return {
            "host": "0.0.0.0",
            "port": 8001,
            "db_path": "daemon_queue.db",
            "lifecycle_mode": "service",
            "start_on_boot": True
        }

    def run(self):
        """Start daemon service"""
        logger.info("Starting Magic Daemon...")
        logger.info(f"API listening on {self.config['host']}:{self.config['port']}")

        uvicorn.run(
            self.api_app,
            host=self.config["host"],
            port=self.config["port"],
            log_level="info"
        )

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Magic PC Daemon")
    parser.add_argument("--config", default="daemon.config.json", help="Config file path")
    args = parser.parse_args()

    daemon = MagicDaemon(Path(args.config))
    daemon.run()
```

**Step 3: Commit**

```bash
git add tools/daemon/api.py tools/daemon/daemon.py
git commit -m "feat: implement FastAPI REST API and main daemon service"
```

---

## Phase 2: Webapp Refactoring

### Task 6: Redirect Webapp Device Calls to Daemon

**Files:**
- Modify: `tools/webapp/server.py`

**Step 1: Create daemon client wrapper**

File: `tools/webapp/daemon_client.py`

```python
import aiohttp
from typing import Optional, Dict, List
import logging

logger = logging.getLogger(__name__)

class DaemonClient:
    """Client for communicating with PC Daemon"""

    def __init__(self, daemon_url: str = "http://localhost:8001"):
        self.daemon_url = daemon_url
        self.session: Optional[aiohttp.ClientSession] = None

    async def connect(self):
        """Initialize HTTP session"""
        self.session = aiohttp.ClientSession()

    async def disconnect(self):
        """Close HTTP session"""
        if self.session:
            await self.session.close()

    async def list_nodes(self) -> List[Dict]:
        """Get list of all nodes"""
        async with self.session.get(f"{self.daemon_url}/api/nodes") as resp:
            if resp.status == 200:
                return await resp.json()
            logger.error(f"Failed to list nodes: {resp.status}")
            return []

    async def send_command(self, node_id: str, command: str) -> bool:
        """Send command to device via daemon"""
        payload = {"dest": node_id, "command": command}
        async with self.session.post(f"{self.daemon_url}/api/command", json=payload) as resp:
            if resp.status == 200:
                result = await resp.json()
                return result.get("status") == "SENT"
            logger.error(f"Command failed: {resp.status}")
            return False

    async def get_messages(self, status: Optional[str] = None) -> List[Dict]:
        """Get command message history"""
        params = {}
        if status:
            params["status"] = status

        async with self.session.get(f"{self.daemon_url}/api/messages", params=params) as resp:
            if resp.status == 200:
                return await resp.json()
            return []

    async def health(self) -> bool:
        """Check daemon health"""
        try:
            async with self.session.get(f"{self.daemon_url}/health") as resp:
                return resp.status == 200
        except:
            return False
```

**Step 2: Modify webapp server to use daemon client**

In `tools/webapp/server.py`, find the `TransportManager` class and replace direct device calls with daemon client calls. This is a large refactoring, so focus on:

- Replace all `self._send_http()` calls with `daemon_client.send_command()`
- Replace `TransportManager.send_command()` with daemon client calls
- Keep WebSocket functionality as-is (broadcasting to browser still works)

Rough changes:
```python
# OLD: Direct device communication
async def send_command(self, cmd: str, node_id: Optional[str] = None) -> bool:
    # ... transport selection logic ...
    return await self._send_http(cmd, ip)

# NEW: Via daemon
async def send_command(self, cmd: str, node_id: Optional[str] = None) -> bool:
    if not node_id:
        return False
    return await self.daemon_client.send_command(node_id, cmd)
```

**Step 3: Initialize daemon client in webapp startup**

In the main FastAPI app startup, initialize the daemon client:

```python
daemon_client = DaemonClient()

@app.on_event("startup")
async def startup():
    await daemon_client.connect()
    logger.info("Connected to daemon")

@app.on_event("shutdown")
async def shutdown():
    await daemon_client.disconnect()
    logger.info("Disconnected from daemon")
```

**Step 4: Commit**

```bash
git add tools/webapp/daemon_client.py
git commit -m "feat: add daemon client wrapper for webapp"
```

---

### Task 7: Update Webapp WebSocket to Show Daemon Node List

**Files:**
- Modify: `tools/webapp/server.py`

**Step 1: Update node list endpoint to fetch from daemon**

Replace the existing `/api/nodes` endpoint:

```python
@app.get("/api/nodes")
async def get_nodes():
    """Get node list from daemon"""
    nodes = await daemon_client.list_nodes()
    return nodes
```

**Step 2: Update WebSocket broadcast to include daemon nodes**

Modify the WebSocket status loop to fetch daemon node list:

```python
# In the status polling loop:
async def poll_status():
    while True:
        nodes = await daemon_client.list_nodes()
        await ws_manager.broadcast({
            "type": "nodes",
            "data": nodes
        })
        await asyncio.sleep(5)  # Poll every 5 seconds
```

**Step 3: Commit**

```bash
git add tools/webapp/server.py
git commit -m "feat: update webapp to fetch node list from daemon"
```

---

## Phase 3: Testing & Validation

### Task 8: Integration Test — Send Command Via Daemon

**Files:**
- Create: `tests/integration/test_daemon_webapp.py`

**Step 1: Write integration test**

```python
import pytest
import asyncio
from tools.daemon.daemon import MagicDaemon
from tools.daemon.models import Node
from tools.webapp.daemon_client import DaemonClient
from pathlib import Path
import tempfile

@pytest.mark.asyncio
async def test_send_command_daemon_to_device():
    """Test sending command from webapp through daemon to device"""

    with tempfile.TemporaryDirectory() as tmpdir:
        # Start daemon
        config_path = Path(tmpdir) / "daemon.config.json"
        daemon = MagicDaemon(config_path)

        # Add test node to daemon
        test_node = Node(id="test-node", name="Test", type="wifi", address="192.168.1.50")
        daemon.api_app.nodes = [test_node]  # Inject for testing

        # Connect client
        client = DaemonClient(daemon_url="http://localhost:8001")

        # List nodes
        nodes = await client.list_nodes()
        assert len(nodes) > 0
        assert nodes[0]["id"] == "test-node"

        # Send command
        success = await client.send_command("test-node", "GPIO 5 HIGH")
        assert success

        # Verify message in queue
        messages = await client.get_messages(status="SENT")
        assert len(messages) > 0
```

**Step 2: Run test**

```bash
pytest tests/integration/test_daemon_webapp.py -v
```

Expected: PASS

**Step 3: Commit**

```bash
git add tests/integration/test_daemon_webapp.py
git commit -m "test: add integration test for daemon command routing"
```

---

## Phase 4: Documentation & Deployment

### Task 9: Write Daemon Deployment Guide

**Files:**
- Create: `docs/daemon-deployment.md`

Content should cover:
- How to start daemon as Windows service (using `pyinstaller` + `NSSM`)
- How to start daemon as systemd service (Linux)
- Configuration options (port, database path, lifecycle mode)
- Connecting phone/webapp to daemon
- Troubleshooting

**Step 1: Create deployment guide**

```markdown
# PC Daemon Deployment Guide

## Quick Start

```bash
python tools/daemon/daemon.py --config daemon.config.json
```

## Windows Service (Always Running)

Install as Windows service using NSSM:

```bash
nssm install MagicDaemon "python" "C:\path\to\daemon.py"
nssm set MagicDaemon AppDirectory "C:\path\to\tools\daemon"
nssm start MagicDaemon
```

## Linux systemd Service

Create `/etc/systemd/system/magic-daemon.service`:

```ini
[Unit]
Description=Magic PC Daemon
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/magic
ExecStart=/usr/bin/python3 /home/pi/magic/tools/daemon/daemon.py
Restart=always

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable magic-daemon
sudo systemctl start magic-daemon
```

## Configuration

Edit `daemon.config.json`:

```json
{
  "host": "0.0.0.0",
  "port": 8001,
  "db_path": "daemon_queue.db",
  "lifecycle_mode": "service",
  "start_on_boot": true
}
```

## Connecting Clients

**Webapp (same machine):**
```
http://localhost:8001
```

**Phone (same LAN):**
```
http://<pc-ip>:8001
```

**Remote (via VPN/tunnel):**
```
https://daemon.your-domain.com
```
```

**Step 2: Commit**

```bash
git add docs/daemon-deployment.md
git commit -m "docs: add daemon deployment and configuration guide"
```

---

## Summary of Changes

| Component | Status | Notes |
|-----------|--------|-------|
| Daemon Foundation | ✅ Tasks 1-5 | REST API, SQLite, transport abstraction |
| Webapp Refactoring | ✅ Tasks 6-7 | Remove direct device calls, use daemon |
| Testing | ✅ Task 8 | Integration tests for command routing |
| Deployment | ✅ Task 9 | Guides for Windows/Linux services |

---

## Next Steps After Implementation

1. **Test on V4-Bravo** — Run daemon, send commands from webapp
2. **Add Mobile Client** — Implement phone app that connects to daemon
3. **Add Internet Bridge** — MQTT forwarding, cloud relay
4. **Add Authentication** — Secure daemon API (JWT tokens, TLS)
5. **Performance Tuning** — Profile message queue, optimize transports
