import aiohttp
from abc import ABC, abstractmethod
from typing import Optional, Dict, List
import logging

logger = logging.getLogger(__name__)


class BaseDeviceClient(ABC):
    """Abstract interface for device communication clients.

    Any client that talks to Magic devices — whether through the PC Daemon,
    directly via HTTP to an ESP32, or via BLE — must implement this interface.

    The webapp holds a BaseDeviceClient reference and never needs to know
    which concrete implementation is in use.

    Implementations:
        DaemonClient      — routes via PC Daemon at localhost:8001
        DirectHTTPClient  — calls ESP32 HTTP API directly (future)
    """

    @abstractmethod
    async def connect(self) -> None:
        """Initialize connection. Call at app startup."""

    @abstractmethod
    async def disconnect(self) -> None:
        """Tear down connection. Call at app shutdown."""

    @abstractmethod
    async def health(self) -> bool:
        """Return True if the backend is reachable."""

    @abstractmethod
    async def list_nodes(self) -> List[Dict]:
        """Return list of known nodes as dicts."""

    @abstractmethod
    async def send_command(self, node_id: str, command: str) -> bool:
        """Send command to device. Return True if delivered."""

    @abstractmethod
    async def get_messages(self, status: Optional[str] = None, dest: Optional[str] = None) -> List[Dict]:
        """Return message history with optional filters."""


class DaemonClient(BaseDeviceClient):
    """HTTP client for communicating with the Magic PC Daemon.

    The webapp uses this to delegate ALL device communication to the daemon,
    which handles transport selection (HTTP/BLE/Serial/LoRa/MQTT) internally.
    """

    def __init__(self, daemon_url: str = "http://localhost:8001"):
        self.daemon_url = daemon_url.rstrip("/")
        self.session: Optional[aiohttp.ClientSession] = None

    async def connect(self) -> None:
        """Initialize HTTP session. Call at app startup."""
        self.session = aiohttp.ClientSession()
        logger.info(f"DaemonClient connected to {self.daemon_url}")

    async def disconnect(self) -> None:
        """Close HTTP session. Call at app shutdown."""
        if self.session:
            await self.session.close()
            self.session = None
            logger.info("DaemonClient disconnected")

    async def health(self) -> bool:
        """Check if daemon is reachable."""
        try:
            async with self.session.get(
                f"{self.daemon_url}/health",
                timeout=aiohttp.ClientTimeout(total=2),
            ) as resp:
                return resp.status == 200
        except Exception as e:
            logger.warning(f"Daemon health check failed: {e}")
            return False

    async def list_nodes(self) -> List[Dict]:
        """Fetch all registered nodes from daemon."""
        try:
            async with self.session.get(f"{self.daemon_url}/api/nodes") as resp:
                if resp.status == 200:
                    return await resp.json()
                logger.error(f"list_nodes failed: HTTP {resp.status}")
                return []
        except Exception as e:
            logger.error(f"list_nodes error: {e}")
            return []

    async def send_command(self, node_id: str, command: str) -> bool:
        """Route a command to a device through the daemon.

        Args:
            node_id: Target node identifier
            command: Command string (e.g. "GPIO 5 HIGH", "RELAY 1 ON")

        Returns:
            True if daemon confirmed SENT, False otherwise
        """
        payload = {"dest": node_id, "command": command}
        try:
            async with self.session.post(
                f"{self.daemon_url}/api/command", json=payload
            ) as resp:
                if resp.status == 200:
                    result = await resp.json()
                    return result.get("status") == "SENT"
                logger.error(f"send_command failed: HTTP {resp.status}")
                return False
        except Exception as e:
            logger.error(f"send_command error for {node_id}: {e}")
            return False

    async def get_messages(
        self, status: Optional[str] = None, dest: Optional[str] = None
    ) -> List[Dict]:
        """Get command message history from daemon."""
        params = {}
        if status:
            params["status"] = status
        if dest:
            params["dest"] = dest

        try:
            async with self.session.get(
                f"{self.daemon_url}/api/messages", params=params
            ) as resp:
                if resp.status == 200:
                    return await resp.json()
                return []
        except Exception as e:
            logger.error(f"get_messages error: {e}")
            return []

    async def provision_device(
        self,
        device_id: str,
        carrier: str,
        features: Optional[Dict] = None,
        identity: Optional[Dict] = None,
        reboot: bool = True,
    ) -> Dict:
        """Provision a device with carrier profile and feature flags.

        Args:
            device_id: Target node identifier
            carrier: Carrier board profile name (e.g. "rv12v", "bare")
            features: Feature overrides {mqtt: 0, gps: 1, ...}
            identity: Device identity {name, role, fleet_id}
            reboot: Whether device should reboot after provisioning

        Returns:
            Response dict with status and reboot_in_ms, or error info
        """
        payload = {
            "device_id": device_id,
            "carrier": carrier,
            "features": features or {},
            "identity": identity or {},
            "reboot": reboot,
        }
        try:
            async with self.session.post(
                f"{self.daemon_url}/api/provision", json=payload
            ) as resp:
                if resp.status == 200:
                    return await resp.json()
                error_text = await resp.text()
                logger.error(f"provision_device failed HTTP {resp.status}: {error_text}")
                return {"status": "error", "error_msg": error_text}
        except Exception as e:
            logger.error(f"provision_device error for {device_id}: {e}")
            return {"status": "error", "error_msg": str(e)}

    async def list_carriers(self) -> List[Dict]:
        """Fetch available carrier board profiles from daemon."""
        try:
            async with self.session.get(f"{self.daemon_url}/api/carriers") as resp:
                if resp.status == 200:
                    return await resp.json()
                return []
        except Exception as e:
            logger.error(f"list_carriers error: {e}")
            return []
