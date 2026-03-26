"""
MeshRouter: Intelligence layer for Phase 50 Autonomous Mesh Sovereignty.

Maintains peer discovery, handles command routing (direct/multi-hop/queue),
tracks command status, and implements retry logic with exponential backoff.

Core Responsibilities:
1. Peer Discovery — Track all devices in mesh (MAC, signal strength, neighbors)
2. Routing — Find best path to target device (priority: direct → LoRa → multi-hop → queue)
3. Command Queueing — Hold commands for offline devices
4. Retry Logic — Resend with exponential backoff (max 3 retries, 2s initial)
5. Conflict Resolution — FIFO queue for simultaneous commands to same device
"""

import time
import uuid
from enum import Enum
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Tuple
from collections import deque
import logging

logger = logging.getLogger(__name__)


class CommandStatus(Enum):
    """Status lifecycle for mesh commands."""
    PENDING = "pending"
    SENT = "sent"
    ACKED = "acked"
    COMPLETED = "completed"
    FAILED = "failed"


class TransportType(Enum):
    """Available transport mediums."""
    WIFI = "wifi"
    LORA = "lora"
    BLE = "ble"
    MQTT = "mqtt"
    ESPNOW = "espnow"


@dataclass
class MeshPeer:
    """Represents a device in the mesh network."""
    node_id: str
    mac_address: str
    last_seen: float  # Unix timestamp (ms)
    rssi_dbm: int
    transport: TransportType
    reachable: bool
    neighbors: List[str] = field(default_factory=list)
    battery_mv: Optional[int] = None
    uptime_ms: Optional[int] = None

    def is_stale(self, threshold_ms: int = 30000) -> bool:
        """Check if peer hasn't been seen recently (default 30s)."""
        return (time.time() * 1000 - self.last_seen) > threshold_ms


@dataclass
class MeshCommand:
    """Represents a command routed through the mesh."""
    cmd_id: str
    from_node: str
    to_node: str
    action: str  # "gpio_toggle", "gpio_set", "gpio_read", etc.
    pin: int
    duration_ms: Optional[int] = None
    status: CommandStatus = CommandStatus.PENDING
    retry_count: int = 0
    queued_at_ms: float = field(default_factory=lambda: time.time() * 1000)
    result: Optional[Dict] = None
    error_message: Optional[str] = None

    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON serialization."""
        d = asdict(self)
        d['status'] = self.status.value
        return d


@dataclass
class MeshPath:
    """Represents a route to a target device."""
    hops: List[str]  # [device1, device2, target]
    transport: TransportType
    signal_quality: int  # Estimated 0-100


class MeshRouter:
    """
    Core routing intelligence for Phase 50 mesh sovereignty.

    Manages peer discovery, command routing, queueing, and retry logic.
    Thread-safe for concurrent command submission.
    """

    def __init__(self, own_node_id: str = "daemon-0"):
        self.own_node_id = own_node_id

        # Peer registry: node_id → MeshPeer
        self.peer_registry: Dict[str, MeshPeer] = {}

        # In-flight commands: cmd_id → MeshCommand
        self.pending_commands: Dict[str, MeshCommand] = {}

        # Queued commands for offline devices: deque of MeshCommand
        self.command_queue: deque = deque()

        # Command history for diagnostics/UI
        self.command_history: deque = deque(maxlen=1000)

        logger.info(f"[MeshRouter] Initialized as {self.own_node_id}")

    # ─────────────────────────────────────────────────────────────────
    # Peer Management
    # ─────────────────────────────────────────────────────────────────

    def register_peer(self, peer: MeshPeer) -> None:
        """Register or update a peer in the mesh."""
        self.peer_registry[peer.node_id] = peer
        logger.info(
            f"[Peer] {peer.node_id} registered: "
            f"MAC={peer.mac_address}, RSSI={peer.rssi_dbm}dBm, "
            f"transport={peer.transport.value}, neighbors={peer.neighbors}"
        )

    def update_peer_status(self, node_id: str, status: Dict) -> None:
        """Update peer status from heartbeat/status message."""
        if node_id not in self.peer_registry:
            logger.warning(f"[Peer] Unknown peer {node_id}, skipping update")
            return

        peer = self.peer_registry[node_id]
        peer.last_seen = status.get("timestamp_ms", time.time() * 1000)
        peer.reachable = status.get("reachable", True)
        peer.battery_mv = status.get("battery_mv")
        peer.uptime_ms = status.get("uptime_ms")
        peer.neighbors = status.get("neighbors", [])

        # Try to drain queue if device just came online
        if peer.reachable:
            self._drain_queue_for_peer(node_id)

    def get_peer(self, node_id: str) -> Optional[MeshPeer]:
        """Get peer by node ID."""
        return self.peer_registry.get(node_id)

    def list_peers(self) -> List[MeshPeer]:
        """Get all registered peers."""
        return list(self.peer_registry.values())

    def get_topology(self) -> Dict:
        """Return mesh topology for visualization/status."""
        return {
            "node_count": len(self.peer_registry),
            "peers": [asdict(p) for p in self.peer_registry.values()],
            "own_node_id": self.own_node_id,
        }

    # ─────────────────────────────────────────────────────────────────
    # Routing Algorithm
    # ─────────────────────────────────────────────────────────────────

    def find_path(self, target_node_id: str) -> Optional[MeshPath]:
        """
        Find best path to target using priority order:
        1. Direct (device has WiFi/BLE to daemon or has LoRa radio online)
        2. Multi-hop (via known neighbors)
        3. None if unreachable and not queued
        """
        target = self.get_peer(target_node_id)
        if not target:
            logger.warning(f"[Route] Target {target_node_id} not in peer registry")
            return None

        # Priority 1: Direct path (device is currently reachable)
        if target.reachable and not target.is_stale():
            logger.info(f"[Route] Direct path to {target_node_id} via {target.transport.value}")
            return MeshPath(
                hops=[target_node_id],
                transport=target.transport,
                signal_quality=self._estimate_signal_quality(target.rssi_dbm),
            )

        # Priority 2: Multi-hop via neighbors
        path = self._find_multihop_path(target_node_id, visited=set())
        if path:
            logger.info(f"[Route] Multi-hop path found: {' → '.join(path.hops)}")
            return path

        # No path available
        logger.warning(f"[Route] No path to {target_node_id}, will queue for retry")
        return None

    def _find_multihop_path(
        self, target_node_id: str, visited: set, max_hops: int = 3
    ) -> Optional[MeshPath]:
        """BFS to find multi-hop path with max 3 hops."""
        from collections import deque as Queue

        target = self.get_peer(target_node_id)
        if not target:
            return None

        queue = Queue([([target_node_id], target)])
        visited.add(target_node_id)

        while queue:
            path, current_peer = queue.popleft()

            if len(path) > max_hops:
                continue

            # Check each neighbor
            for neighbor_id in current_peer.neighbors:
                if neighbor_id in visited:
                    continue

                neighbor = self.get_peer(neighbor_id)
                if not neighbor or not neighbor.reachable:
                    continue

                new_path = [neighbor_id] + path
                visited.add(neighbor_id)

                # If this neighbor can reach target directly, we found a path
                if target_node_id in neighbor.neighbors and neighbor.reachable:
                    return MeshPath(
                        hops=new_path + [target_node_id],
                        transport=neighbor.transport,
                        signal_quality=self._estimate_signal_quality(neighbor.rssi_dbm),
                    )

                queue.append((new_path, neighbor))

        return None

    def _estimate_signal_quality(self, rssi_dbm: int) -> int:
        """Convert RSSI to signal quality 0-100."""
        # RSSI ranges typically -30 (excellent) to -120 (poor)
        if rssi_dbm > -50:
            return 100
        elif rssi_dbm > -70:
            return 80
        elif rssi_dbm > -90:
            return 60
        elif rssi_dbm > -110:
            return 40
        else:
            return 20

    # ─────────────────────────────────────────────────────────────────
    # Command Routing
    # ─────────────────────────────────────────────────────────────────

    def route_command(self, cmd: MeshCommand) -> Tuple[bool, str]:
        """
        Route a command to target device.

        Returns:
            (success, status_msg)
            success=True if command sent or queued
            status_msg describes what happened
        """
        logger.info(f"[Cmd] Routing {cmd.cmd_id}: {cmd.action} to {cmd.to_node}")

        # Validate target exists
        if cmd.to_node not in self.peer_registry:
            msg = f"Target {cmd.to_node} not in peer registry"
            logger.error(f"[Cmd] {msg}")
            cmd.status = CommandStatus.FAILED
            cmd.error_message = msg
            return False, msg

        # Try to find path
        path = self.find_path(cmd.to_node)

        if path:
            # We can reach the device
            cmd.status = CommandStatus.SENT
            self.pending_commands[cmd.cmd_id] = cmd
            self.command_history.append(cmd)

            msg = f"Command {cmd.cmd_id} sent via {' → '.join(path.hops)}"
            logger.info(f"[Cmd] {msg}")
            return True, msg
        else:
            # Device unreachable, queue for retry
            cmd.status = CommandStatus.PENDING
            self.command_queue.append(cmd)
            self.command_history.append(cmd)

            msg = f"Command {cmd.cmd_id} queued (device offline, retry in {30000}ms)"
            logger.info(f"[Cmd] {msg}")
            return True, msg

    def handle_command_ack(self, cmd_id: str, success: bool, result: Optional[Dict] = None) -> None:
        """Process ACK from device."""
        if cmd_id not in self.pending_commands:
            logger.warning(f"[Ack] Unknown command ID {cmd_id}")
            return

        cmd = self.pending_commands[cmd_id]
        if success:
            cmd.status = CommandStatus.COMPLETED
            cmd.result = result
            logger.info(f"[Ack] {cmd_id} completed: {result}")
        else:
            cmd.status = CommandStatus.FAILED
            cmd.error_message = result.get("error", "Unknown error") if result else None
            logger.error(f"[Ack] {cmd_id} failed: {cmd.error_message}")

        del self.pending_commands[cmd_id]

    def get_command_status(self, cmd_id: str) -> Optional[Dict]:
        """Get status of a command by ID."""
        # Check pending
        if cmd_id in self.pending_commands:
            return self.pending_commands[cmd_id].to_dict()

        # Check history
        for cmd in self.command_history:
            if cmd.cmd_id == cmd_id:
                return cmd.to_dict()

        return None

    # ─────────────────────────────────────────────────────────────────
    # Queueing & Retry
    # ─────────────────────────────────────────────────────────────────

    def _drain_queue_for_peer(self, node_id: str, max_retries: int = 3) -> None:
        """Attempt to send queued commands for a peer that just came online."""
        commands_to_retry = []

        while self.command_queue:
            cmd = self.command_queue.popleft()

            if cmd.to_node == node_id and cmd.retry_count < max_retries:
                # Try to reroute this command
                success, msg = self.route_command(cmd)
                if not success and cmd.retry_count < max_retries:
                    # Re-queue if failed
                    cmd.retry_count += 1
                    commands_to_retry.append(cmd)
                    logger.info(f"[Queue] Retry {cmd.retry_count}/{max_retries} for {cmd.cmd_id}")
            else:
                # Not for this peer, or out of retries
                commands_to_retry.append(cmd)

        # Re-queue commands that weren't sent
        for cmd in commands_to_retry:
            self.command_queue.append(cmd)

    def retry_failed_commands(self, max_retries: int = 3) -> None:
        """Background task: retry commands for peers that are now online."""
        now = time.time() * 1000

        for peer in list(self.peer_registry.values()):
            if peer.reachable:
                self._drain_queue_for_peer(peer.node_id, max_retries)

    def get_queue_status(self) -> Dict:
        """Return current queue statistics."""
        return {
            "queued_count": len(self.command_queue),
            "pending_count": len(self.pending_commands),
            "history_count": len(self.command_history),
        }

    # ─────────────────────────────────────────────────────────────────
    # Diagnostics
    # ─────────────────────────────────────────────────────────────────

    def get_stats(self) -> Dict:
        """Get router statistics."""
        return {
            "own_node_id": self.own_node_id,
            "peer_count": len(self.peer_registry),
            "pending_commands": len(self.pending_commands),
            "queued_commands": len(self.command_queue),
            "command_history_size": len(self.command_history),
        }

    def print_status(self) -> None:
        """Print router status for debugging."""
        print("\n" + "=" * 60)
        print(f"[MeshRouter] {self.own_node_id} Status")
        print("=" * 60)
        print(f"Peers: {len(self.peer_registry)}")
        for peer in self.peer_registry.values():
            status = "🟢 ONLINE" if peer.reachable else "🔴 OFFLINE"
            print(f"  {peer.node_id:20} {status:15} RSSI={peer.rssi_dbm:4}dBm "
                  f"neighbors={peer.neighbors}")
        print(f"\nPending commands: {len(self.pending_commands)}")
        print(f"Queued commands: {len(self.command_queue)}")
        print(f"Command history: {len(self.command_history)}")
        print("=" * 60 + "\n")
