import aiohttp
from typing import Optional, Dict, List
import logging

logger = logging.getLogger(__name__)


class DaemonClient:
    """HTTP client for communicating with the LoRaLink PC Daemon.

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
        """Get command message history from daemon.

        Args:
            status: Filter by status ("QUEUED", "SENT", "FAILED", etc.)
            dest: Filter by destination node ID
        """
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
