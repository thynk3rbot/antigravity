"""
MQTT Client for device ↔ daemon communication.

Handles:
- Device status updates (uptime, battery, telemetry)
- Peer list notifications (mesh neighbor discovery)
- Command acknowledgments from devices
- Publishing commands to devices

MQTT Topic Contract (Phase 50 - Aligned with firmware):
  Device → Daemon:
    loralink/{node_id}/telemetry   → {"uptime_ms": ..., "battery_mv": ..., "neighbors": [...]}
    loralink/{node_id}/status      → "ONLINE" / "OFFLINE"
    loralink/{node_id}/msg         → {"cmd_id": "abc123", "status": "ok", "result": {...}}

  Daemon → Device:
    loralink/{node_id}/cmd         ← {"cmd_id": "abc123", "action": "gpio_toggle", "pin": 32, ...}
"""

import asyncio
import json
import logging
from typing import Callable, Optional, Dict, List
from abc import ABC, abstractmethod

logger = logging.getLogger(__name__)


class BaseMQTTClient(ABC):
    """Abstract MQTT client interface."""

    @abstractmethod
    async def connect(self) -> None:
        pass

    @abstractmethod
    async def disconnect(self) -> None:
        pass

    @abstractmethod
    async def subscribe_to_device_topics(self) -> None:
        pass

    @abstractmethod
    async def publish_command(self, node_id: str, cmd: Dict) -> bool:
        pass


class MQTTClientManager(BaseMQTTClient):
    """
    Manager for MQTT communication with LoRaLink devices.

    Callbacks:
    - on_device_status(node_id, status_dict)
    - on_command_ack(cmd_id, success, result_dict)
    """

    def __init__(
        self,
        broker: str = "localhost:1883",
        on_device_status: Optional[Callable] = None,
        on_command_ack: Optional[Callable] = None,
    ):
        self.broker = broker
        self.on_device_status = on_device_status
        self.on_command_ack = on_command_ack

        self.client = None
        self.connected = False
        self.known_devices: Dict[str, dict] = {}

        # Try to import paho-mqtt
        try:
            import paho.mqtt.client as mqtt
            self.mqtt = mqtt
        except ImportError:
            logger.warning("[MQTT] paho-mqtt not installed, using mock client")
            self.mqtt = None

    async def connect(self) -> None:
        """Connect to MQTT broker."""
        if not self.mqtt:
            logger.warning("[MQTT] Using mock client (paho-mqtt not installed)")
            self.connected = True
            return

        try:
            self.client = self.mqtt.Client(self.mqtt.CallbackAPIVersion.VERSION2)
            self.client.on_connect = self._on_connect
            self.client.on_message = self._on_message
            self.client.on_disconnect = self._on_disconnect

            host, port = self._parse_broker_addr(self.broker)
            self.client.connect(host, port, keepalive=60)

            # Start network loop in background thread
            self.client.loop_start()
            self.connected = True

            logger.info(f"[MQTT] Connected to {self.broker}")
        except Exception as e:
            logger.error(f"[MQTT] Connection failed: {e}")
            self.connected = False

    async def disconnect(self) -> None:
        """Disconnect from MQTT broker."""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            self.connected = False
            logger.info("[MQTT] Disconnected")

    async def subscribe_to_device_topics(self) -> None:
        """Subscribe to device topics."""
        if not self.client:
            return

        topics = [
            "loralink/+/telemetry",
            "loralink/+/status",
            "loralink/+/msg",
        ]

        for topic in topics:
            self.client.subscribe(topic)
            logger.info(f"[MQTT] Subscribed to {topic}")

    async def publish_command(self, node_id: str, cmd: Dict) -> bool:
        """Publish command to device for mesh relay."""
        if not self.client:
            # Mock mode - just log it
            logger.info(f"[MQTT] Mock publish to device/{node_id}/mesh/command: {cmd.get('cmd_id', '?')}")
            return True

        if not self.connected:
            logger.warning(f"[MQTT] Not connected, cannot send command to {node_id}")
            return False

        topic = f"loralink/cmd/{node_id}"
        payload = json.dumps(cmd)

        try:
            result = self.client.publish(topic, payload, qos=1)
            if result.rc == self.mqtt.MQTT_ERR_SUCCESS:
                logger.info(f"[MQTT] Published command to {topic}: {cmd.get('cmd_id', '?')}")
                return True
            else:
                logger.error(f"[MQTT] Publish failed (rc={result.rc})")
                return False
        except Exception as e:
            logger.error(f"[MQTT] Publish error: {e}")
            return False

    # ─────────────────────────────────────────────────────────────────
    # MQTT Callbacks
    # ─────────────────────────────────────────────────────────────────

    def _on_connect(self, client, userdata, connect_flags, rc, properties=None):
        """MQTT connect callback."""
        if rc == 0:
            logger.info("[MQTT] Connected successfully")
            self.connected = True
        else:
            logger.error(f"[MQTT] Connection failed with code {rc}")
            self.connected = False

    def _on_disconnect(self, client, userdata, disconnect_flags, rc, properties=None):
        """MQTT disconnect callback."""
        self.connected = False
        if rc != 0:
            logger.warning(f"[MQTT] Unexpected disconnection with code {rc}")
        else:
            logger.info("[MQTT] Disconnected cleanly")

    def _on_message(self, client, userdata, msg):
        """MQTT message callback."""
        try:
            # Parse topic: loralink/{node_id}/{type}
            parts = msg.topic.split("/")
            if len(parts) < 3 or parts[0] != "loralink":
                logger.warning(f"[MQTT] Unknown topic format: {msg.topic}")
                return

            node_id = parts[1]
            msg_type = parts[2]

            # Decode payload
            try:
                payload = json.loads(msg.payload.decode("utf-8"))
            except json.JSONDecodeError:
                logger.warning(f"[MQTT] Invalid JSON from {msg.topic}: {msg.payload}")
                return

            logger.debug(f"[MQTT] {msg.topic}: {payload}")

            # Route by message type
            if msg_type == "telemetry":
                self._handle_device_status(node_id, payload)
            elif msg_type == "status":
                # ONLINE/OFFLINE handled if needed
                pass
            elif msg_type == "msg":
                self._handle_command_ack(payload)
            else:
                logger.warning(f"[MQTT] Unknown message type: {msg_type}")

        except Exception as e:
            logger.error(f"[MQTT] Error processing message: {e}")

    def _handle_device_status(self, node_id: str, status: Dict) -> None:
        """Process device status message."""
        logger.info(f"[Status] {node_id}: uptime={status.get('uptime_ms', '?')}ms, "
                   f"battery={status.get('battery_mv', '?')}mV")

        # Add timestamp if not present
        if "timestamp_ms" not in status:
            import time
            status["timestamp_ms"] = time.time() * 1000

        self.known_devices[node_id] = status

        # Invoke callback if registered
        if self.on_device_status:
            asyncio.create_task(self.on_device_status(node_id, status))

    def _handle_peer_list(self, node_id: str, peers_msg: Dict) -> None:
        """Process peer list (neighbor discovery)."""
        neighbors = peers_msg.get("neighbors", [])
        rssi_values = peers_msg.get("rssi", [])

        logger.info(f"[Peers] {node_id} sees neighbors: {neighbors}")

        # For now, we'll fold this into device status
        if node_id in self.known_devices:
            self.known_devices[node_id]["neighbors"] = neighbors
            self.known_devices[node_id]["neighbor_rssi"] = rssi_values

    def _handle_command_ack(self, ack_msg: Dict) -> None:
        """Process command acknowledgment."""
        cmd_id = ack_msg.get("cmd_id", "unknown")
        status = ack_msg.get("status", "unknown")
        result = ack_msg.get("result", {})

        success = status == "ok"
        logger.info(f"[Ack] {cmd_id}: {status}")

        # Invoke callback if registered
        if self.on_command_ack:
            asyncio.create_task(
                self.on_command_ack(cmd_id, success, result)
            )

    # ─────────────────────────────────────────────────────────────────
    # Utilities
    # ─────────────────────────────────────────────────────────────────

    @staticmethod
    def _parse_broker_addr(broker: str) -> tuple:
        """Parse 'host:port' or 'host' into (host, port)."""
        if ":" in broker:
            host, port = broker.split(":", 1)
            return host, int(port)
        return broker, 1883

    def get_known_devices(self) -> List[str]:
        """Get list of known devices."""
        return list(self.known_devices.keys())

    def get_device_status(self, node_id: str) -> Optional[Dict]:
        """Get last known status of a device."""
        return self.known_devices.get(node_id)

    def print_status(self) -> None:
        """Print MQTT client status."""
        print("\n" + "=" * 60)
        print(f"[MQTT] Status")
        print("=" * 60)
        print(f"Broker: {self.broker}")
        print(f"Connected: {self.connected}")
        print(f"Known devices: {len(self.known_devices)}")
        for node_id, status in self.known_devices.items():
            uptime = status.get("uptime_ms", "?")
            battery = status.get("battery_mv", "?")
            neighbors = status.get("neighbors", [])
            print(f"  {node_id:20} uptime={uptime}ms battery={battery}mV neighbors={neighbors}")
        print("=" * 60 + "\n")
