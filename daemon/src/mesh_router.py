"""
MeshTopology: Topology monitor for pure peer-to-peer mesh.

Pure Mesh Model:
- Devices route to each other via ControlPacket (LoRa/BLE/ESP-NOW)
- All devices understand all capabilities (even if they don't have them)
- Devices relay packets for other devices automatically
- Daemon monitors topology only (reads MQTT status messages)
- Daemon provides visibility + acts as command injection point for webapp

Daemon role:
1. Track all peers (from MQTT status updates)
2. Monitor mesh topology in real-time
3. Provide REST API for webapp to inject commands
4. Publish commands to MQTT for devices to mesh-relay
5. Track command status (device ACKs)
"""

import time
import uuid
from enum import Enum
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional
from collections import deque
import logging

logger = logging.getLogger(__name__)


class CommandStatus(Enum):
    """Status lifecycle for mesh commands."""
    PENDING = "pending"
    PUBLISHED = "published"  # Published to MQTT for devices to relay
    ACKED = "acked"
    COMPLETED = "completed"
    FAILED = "failed"


@dataclass
class MeshPeer:
    """Represents a device in the mesh network."""
    node_id: str
    mac_address: str
    last_seen: float  # Unix timestamp (ms)
    rssi_dbm: int
    reachable: bool
    neighbors: List[str] = field(default_factory=list)
    battery_mv: Optional[int] = None
    uptime_ms: Optional[int] = None

    def is_stale(self, threshold_ms: int = 30000) -> bool:
        """Check if peer hasn't been seen recently (default 30s)."""
        return (time.time() * 1000 - self.last_seen) > threshold_ms


@dataclass
class MeshCommand:
    """Represents a command injected into the mesh."""
    cmd_id: str
    from_node: str  # Usually "daemon-api" or "webapp"
    to_node: str
    action: str  # "gpio_toggle", "gpio_set", "gpio_read", etc.
    pin: int
    duration_ms: Optional[int] = None
    status: CommandStatus = CommandStatus.PENDING
    published_at_ms: float = field(default_factory=lambda: time.time() * 1000)
    result: Optional[Dict] = None
    error_message: Optional[str] = None

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        d = asdict(self)
        d['status'] = self.status.value
        return d


class MeshTopology:
    """
    Topology monitor for pure peer-to-peer mesh.

    Passive monitoring:
    - Listens to device status updates
    - Tracks peer registry (MAC, signal, neighbors, battery, uptime)
    - Maintains command history for status tracking

    Active gateway:
    - Publishes commands to MQTT for devices to relay
    - Tracks ACKs from devices
    - Provides REST API for webapp
    """

    def __init__(self, own_node_id: str = "daemon-0"):
        self.own_node_id = own_node_id

        # Peer registry: node_id → MeshPeer
        self.peer_registry: Dict[str, MeshPeer] = {}

        # In-flight commands: cmd_id → MeshCommand
        self.active_commands: Dict[str, MeshCommand] = {}

        # Command history for diagnostics/UI
        self.command_history: deque = deque(maxlen=1000)

        logger.info(f"[Mesh] Topology monitor initialized as {self.own_node_id}")

    # ─────────────────────────────────────────────────────────────────
    # Peer Management (Passive Monitoring)
    # ─────────────────────────────────────────────────────────────────

    def register_peer(self, peer: MeshPeer) -> None:
        """Register or update a peer in the mesh."""
        self.peer_registry[peer.node_id] = peer
        logger.info(
            f"[Peer] {peer.node_id} registered: "
            f"MAC={peer.mac_address}, RSSI={peer.rssi_dbm}dBm, "
            f"neighbors={peer.neighbors}"
        )

    def update_peer_status(self, node_id: str, status: Dict) -> None:
        """Update peer status from heartbeat/status message."""
        if node_id not in self.peer_registry:
            # New device discovered
            peer = MeshPeer(
                node_id=node_id,
                mac_address=status.get("mac_address", status.get("mac", "00:00:00:00:00:00")),
                last_seen=status.get("timestamp_ms", time.time() * 1000),
                rssi_dbm=status.get("rssi_dbm", status.get("rssi", -80)),
                reachable=True,
                neighbors=status.get("neighbors", []),
                battery_mv=status.get("battery_mv"),
                uptime_ms=status.get("uptime_ms"),
            )
            self.register_peer(peer)
        else:
            # Update existing peer
            peer = self.peer_registry[node_id]
            peer.last_seen = status.get("timestamp_ms", time.time() * 1000)
            peer.reachable = status.get("reachable", True)
            peer.battery_mv = status.get("battery_mv", peer.battery_mv)
            peer.uptime_ms = status.get("uptime_ms", peer.uptime_ms)
            peer.neighbors = status.get("neighbors", peer.neighbors)
            if "rssi_dbm" in status:
                peer.rssi_dbm = status["rssi_dbm"]
            elif "rssi" in status:
                peer.rssi_dbm = status["rssi"]

            logger.debug(f"[Peer] {node_id} updated: neighbors={peer.neighbors}, rssi={peer.rssi_dbm}dBm")

    def get_peer(self, node_id: str) -> Optional[MeshPeer]:
        """Get peer by node ID."""
        return self.peer_registry.get(node_id)

    def list_peers(self) -> List[MeshPeer]:
        """Get all registered peers."""
        return list(self.peer_registry.values())

    def get_topology(self) -> Dict:
        """Return mesh topology for visualization/status."""
        # Mark stale peers as offline
        for peer in self.peer_registry.values():
            if peer.is_stale(threshold_ms=120000):  # 2 minutes
                peer.reachable = False

        return {
            "node_count": len(self.peer_registry),
            "online_count": sum(1 for p in self.peer_registry.values() if p.reachable),
            "peers": [asdict(p) for p in self.peer_registry.values()],
            "own_node_id": self.own_node_id,
        }

    # ─────────────────────────────────────────────────────────────────
    # Command Management (Gateway)
    # ─────────────────────────────────────────────────────────────────

    def create_command(self, target_node: str, action: str, pin: int, duration_ms: Optional[int] = None) -> MeshCommand:
        """Create a new command to inject into the mesh."""
        cmd = MeshCommand(
            cmd_id=str(uuid.uuid4()),
            from_node="daemon-api",
            to_node=target_node,
            action=action,
            pin=pin,
            duration_ms=duration_ms,
        )
        return cmd

    def publish_command(self, cmd: MeshCommand) -> bool:
        """
        Mark command as published to MQTT.

        Actual MQTT publishing happens in mqtt_client.py.
        This method just tracks the command for status tracking.
        """
        if cmd.to_node not in self.peer_registry:
            logger.warning(f"[Cmd] Target {cmd.to_node} not in peer registry")
            cmd.status = CommandStatus.FAILED
            cmd.error_message = f"Target {cmd.to_node} unknown"
            return False

        cmd.status = CommandStatus.PUBLISHED
        self.active_commands[cmd.cmd_id] = cmd
        self.command_history.append(cmd)

        logger.info(
            f"[Cmd] {cmd.cmd_id} published to MQTT for {cmd.to_node}: "
            f"{cmd.action} pin={cmd.pin}"
        )
        return True

    def handle_command_ack(self, cmd_id: str, success: bool, result: Optional[Dict] = None) -> None:
        """Process ACK from device."""
        if cmd_id not in self.active_commands:
            logger.warning(f"[Ack] Unknown command ID {cmd_id}")
            return

        cmd = self.active_commands[cmd_id]
        if success:
            cmd.status = CommandStatus.COMPLETED
            cmd.result = result
            logger.info(f"[Ack] {cmd_id} completed: {result}")
        else:
            cmd.status = CommandStatus.FAILED
            cmd.error_message = result.get("error", "Unknown error") if result else None
            logger.error(f"[Ack] {cmd_id} failed: {cmd.error_message}")

        del self.active_commands[cmd_id]

    def get_command_status(self, cmd_id: str) -> Optional[Dict]:
        """Get status of a command by ID."""
        # Check active
        if cmd_id in self.active_commands:
            return self.active_commands[cmd_id].to_dict()

        # Check history
        for cmd in self.command_history:
            if cmd.cmd_id == cmd_id:
                return cmd.to_dict()

        return None

    # ─────────────────────────────────────────────────────────────────
    # Diagnostics
    # ─────────────────────────────────────────────────────────────────

    def get_stats(self) -> Dict:
        """Get mesh statistics."""
        online_peers = sum(1 for p in self.peer_registry.values() if p.reachable)
        return {
            "own_node_id": self.own_node_id,
            "total_peers": len(self.peer_registry),
            "online_peers": online_peers,
            "active_commands": len(self.active_commands),
            "command_history_size": len(self.command_history),
        }

    def print_status(self) -> None:
        """Print mesh status for debugging."""
        online = sum(1 for p in self.peer_registry.values() if p.reachable)
        print("\n" + "=" * 70)
        print(f"[Mesh Topology] {self.own_node_id}")
        print("=" * 70)
        print(f"Peers: {len(self.peer_registry)} ({online} online)")

        for peer in sorted(self.peer_registry.values(), key=lambda p: p.node_id):
            status = "ONLINE" if peer.reachable else "OFFLINE"
            neighbors_str = ", ".join(peer.neighbors) if peer.neighbors else "(none)"
            print(f"  {peer.node_id:15} {status:8} RSSI={peer.rssi_dbm:4}dBm "
                  f"neighbors=[{neighbors_str}]")

        print(f"\nActive Commands: {len(self.active_commands)}")
        print(f"Command History: {len(self.command_history)}")
        print("=" * 70 + "\n")
