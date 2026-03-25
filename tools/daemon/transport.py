from typing import Optional
from tools.daemon.models import Transport, Node
import logging

logger = logging.getLogger(__name__)


class TransportManager:
    """Manages device communication across multiple transports with intelligent fallback"""

    def __init__(self):
        self._http_handler = HTTPTransport()
        self._ble_handler = BLETransport()
        self._serial_handler = SerialTransport()
        self._lora_handler = LoRaTransport()
        self._mqtt_handler = MQTTTransport()

    async def select_transport(self, node: Node) -> Transport:
        """
        Intelligently select best transport for node.
        Tries preferred transport first, then falls back to others.

        Args:
            node: Target device node

        Returns:
            Selected Transport enum value

        Raises:
            RuntimeError if no transport is available
        """
        # If node has preferred transport and it's available, use it
        if node.preferred_transport:
            if await self._probe_transport(node, node.preferred_transport):
                logger.info(f"Using preferred transport {node.preferred_transport.value} for {node.name}")
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
        """Test if transport is available for node

        Args:
            node: Target device node
            transport: Transport to probe

        Returns:
            True if transport is reachable, False otherwise
        """
        try:
            if transport == Transport.HTTP:
                return await self._probe_http(node)
            elif transport == Transport.BLE:
                return await self._probe_ble(node)
            elif transport == Transport.SERIAL:
                return await self._probe_serial(node)
            elif transport == Transport.LORA:
                return await self._probe_lora(node)
            elif transport == Transport.MQTT:
                return await self._probe_mqtt(node)
            return False
        except Exception as e:
            logger.debug(f"Probe {transport.value} failed for {node.name}: {e}")
            return False

    async def _probe_http(self, node: Node) -> bool:
        """Check if HTTP is reachable"""
        return await self._http_handler.is_reachable(node)

    async def _probe_ble(self, node: Node) -> bool:
        """Check if BLE is reachable"""
        return await self._ble_handler.is_reachable(node)

    async def _probe_serial(self, node: Node) -> bool:
        """Check if Serial is reachable"""
        return await self._serial_handler.is_reachable(node)

    async def _probe_lora(self, node: Node) -> bool:
        """Check if LoRa is reachable"""
        return await self._lora_handler.is_reachable(node)

    async def _probe_mqtt(self, node: Node) -> bool:
        """Check if MQTT is reachable"""
        return await self._mqtt_handler.is_reachable(node)

    async def send_command(self, node: Node, command: str) -> bool:
        """Send command to node via best available transport

        Args:
            node: Target device node
            command: Command string to send

        Returns:
            True if send successful, False otherwise
        """
        try:
            transport = await self.select_transport(node)

            if transport == Transport.HTTP:
                return await self._http_handler.send(node, command)
            elif transport == Transport.BLE:
                return await self._ble_handler.send(node, command)
            elif transport == Transport.SERIAL:
                return await self._serial_handler.send(node, command)
            elif transport == Transport.LORA:
                return await self._lora_handler.send(node, command)
            elif transport == Transport.MQTT:
                return await self._mqtt_handler.send(node, command)
            else:
                logger.error(f"No handler for transport {transport.value}")
                return False
        except Exception as e:
            logger.error(f"Failed to send command to {node.name}: {e}")
            return False


class TransportHandler:
    """Base class for transport handlers"""

    async def is_reachable(self, node: Node) -> bool:
        """Check if transport is available for node

        Args:
            node: Target device node

        Returns:
            True if reachable, False otherwise
        """
        raise NotImplementedError

    async def send(self, node: Node, command: str) -> bool:
        """Send command via this transport

        Args:
            node: Target device node
            command: Command string to send

        Returns:
            True if send successful, False otherwise
        """
        raise NotImplementedError


class HTTPTransport(TransportHandler):
    """HTTP transport handler for WiFi-based devices"""

    async def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual HTTP probe (HEAD request, timeout)
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual HTTP send (POST to device API)
        return True


class BLETransport(TransportHandler):
    """BLE transport handler for Bluetooth Low Energy devices"""

    async def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual BLE probe (scan, RSSI check)
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual BLE send (write characteristic)
        return True


class SerialTransport(TransportHandler):
    """Serial transport handler for direct USB/Serial connections"""

    async def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual Serial probe (port check, handshake)
        return True

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual Serial send (write to port)
        return True


class LoRaTransport(TransportHandler):
    """LoRa transport handler for long-range wireless devices"""

    async def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual LoRa probe (check mesh connectivity)
        return False

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual LoRa send (mesh routing)
        return False


class MQTTTransport(TransportHandler):
    """MQTT transport handler for IoT broker-based communication"""

    async def is_reachable(self, node: Node) -> bool:
        # TODO: Implement actual MQTT probe (broker check, topic subscription)
        return False

    async def send(self, node: Node, command: str) -> bool:
        # TODO: Implement actual MQTT send (publish to device topic)
        return False
