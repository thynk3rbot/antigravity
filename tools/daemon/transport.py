from typing import Optional
from tools.daemon.models import Transport, Node
import asyncio
import logging
import re

logger = logging.getLogger(__name__)

# Optional imports — degrade gracefully if not installed
try:
    import aiohttp
except ImportError:
    aiohttp = None

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    BleakClient = None
    BleakScanner = None

try:
    import serial
except ImportError:
    serial = None

# BLE Nordic UART Service UUIDs (matches firmware BLEManager)
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # PC → device
NUS_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # device → PC

# Timeouts (seconds)
HTTP_CMD_TIMEOUT = 3.0
HTTP_PROBE_TIMEOUT = 2.5
BLE_SCAN_TIMEOUT = 8.0
BLE_CONNECT_TIMEOUT = 10.0
BLE_ATT_MTU = 20
SERIAL_BAUD = 115200
SERIAL_TIMEOUT = 2.0


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

    def __init__(self):
        self._session: Optional["aiohttp.ClientSession"] = None

    async def _get_session(self) -> "aiohttp.ClientSession":
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession()
        return self._session

    def _get_ip(self, node: Node) -> Optional[str]:
        addr = node.address
        if re.match(r"^\d{1,3}(\.\d{1,3}){3}$", addr):
            return addr
        return None

    async def is_reachable(self, node: Node) -> bool:
        if not aiohttp:
            return False
        ip = self._get_ip(node)
        if not ip:
            return False
        try:
            session = await self._get_session()
            async with session.get(
                f"http://{ip}/api/status",
                timeout=aiohttp.ClientTimeout(total=HTTP_PROBE_TIMEOUT),
            ) as r:
                return r.status == 200
        except Exception:
            return False

    async def send(self, node: Node, command: str) -> bool:
        if not aiohttp:
            return False
        ip = self._get_ip(node)
        if not ip:
            return False
        try:
            session = await self._get_session()
            async with session.post(
                f"http://{ip}/api/cmd",
                data={"cmd": command},
                timeout=aiohttp.ClientTimeout(total=HTTP_CMD_TIMEOUT),
            ) as r:
                return r.status == 200
        except Exception as e:
            logger.error(f"HTTP send to {ip} failed: {e}")
            return False


class BLETransport(TransportHandler):
    """BLE transport handler using Nordic UART Service (connect-per-command)"""

    def _is_ble_address(self, addr: str) -> bool:
        return bool(re.match(r"^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$", addr))

    async def _find_device(self, node: Node):
        """Find BLE device by MAC address or name prefix"""
        if not BleakScanner:
            return None
        devices = await BleakScanner.discover(timeout=BLE_SCAN_TIMEOUT)
        # Match by MAC address
        if self._is_ble_address(node.address):
            for d in devices:
                if d.address.upper() == node.address.upper():
                    return d
            return None
        # Match by name prefix
        for d in devices:
            if d.name and d.name.startswith(node.address):
                return d
        return None

    async def is_reachable(self, node: Node) -> bool:
        if not BleakClient:
            return False
        try:
            device = await self._find_device(node)
            return device is not None
        except Exception:
            return False

    async def send(self, node: Node, command: str) -> bool:
        if not BleakClient:
            return False
        try:
            device = await self._find_device(node)
            if not device:
                logger.error(f"BLE device not found for {node.name}")
                return False

            async with BleakClient(device, timeout=BLE_CONNECT_TIMEOUT) as client:
                # Find RX characteristic (PC → device)
                rx_uuid = None
                for service in client.services:
                    if service.uuid.upper() == NUS_SERVICE_UUID.upper():
                        for char in service.characteristics:
                            if char.uuid.upper() == NUS_RX_CHAR_UUID.upper():
                                rx_uuid = char.uuid
                if not rx_uuid:
                    logger.error(f"NUS RX characteristic not found on {node.name}")
                    return False

                # Write command in chunks (ATT_MTU safe)
                payload = (command + "\n").encode("utf-8")
                for i in range(0, len(payload), BLE_ATT_MTU):
                    await client.write_gatt_char(
                        rx_uuid, payload[i:i + BLE_ATT_MTU], response=False
                    )
                return True
        except Exception as e:
            logger.error(f"BLE send to {node.name} failed: {e}")
            return False


class SerialTransport(TransportHandler):
    """Serial transport handler for direct USB/Serial connections"""

    def _is_serial_port(self, addr: str) -> bool:
        return bool(re.match(r"^(COM\d+|/dev/tty(USB|ACM|S)\d+)$", addr, re.IGNORECASE))

    async def is_reachable(self, node: Node) -> bool:
        if not serial or not self._is_serial_port(node.address):
            return False
        loop = asyncio.get_event_loop()
        try:
            def _probe():
                import time
                with serial.Serial(node.address, SERIAL_BAUD, timeout=SERIAL_TIMEOUT) as ser:
                    ser.flushInput()
                    ser.write(b"STATUS\n")
                    buf = b""
                    end = time.monotonic() + SERIAL_TIMEOUT
                    while time.monotonic() < end:
                        chunk = ser.read(ser.in_waiting or 1)
                        if chunk:
                            buf += chunk
                            if b"\n" in buf:
                                return True
                    return len(buf) > 0
            return await loop.run_in_executor(None, _probe)
        except Exception:
            return False

    async def send(self, node: Node, command: str) -> bool:
        if not serial or not self._is_serial_port(node.address):
            return False
        loop = asyncio.get_event_loop()
        try:
            def _send():
                with serial.Serial(node.address, SERIAL_BAUD, timeout=SERIAL_TIMEOUT) as ser:
                    ser.flushInput()
                    ser.write((command + "\n").encode("utf-8"))
                    ser.flush()
                    return True
            return await loop.run_in_executor(None, _send)
        except Exception as e:
            logger.error(f"Serial send to {node.address} failed: {e}")
            return False


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
