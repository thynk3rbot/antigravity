"""
HTTP Gateway: Routes commands to global devices via direct HTTP.

Instead of: Daemon → MQTT Broker → Device
Use:        Daemon → HTTP POST → Device API

Handles:
- Device IP lookup (from registry)
- Timeout + retry logic
- Fallback to MQTT if HTTP fails
- Command tracking across transports
"""

import asyncio
import aiohttp
import logging
from typing import Dict, Optional, Any
from datetime import datetime

logger = logging.getLogger(__name__)


class HTTPGateway:
    """Route mesh commands to devices via HTTP."""

    def __init__(self, device_registry, timeout_sec: int = 10, retries: int = 2):
        self.registry = device_registry
        self.timeout_sec = timeout_sec
        self.retries = retries
        self.session: Optional[aiohttp.ClientSession] = None

    async def initialize(self):
        """Create HTTP session."""
        self.session = aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=self.timeout_sec)
        )

    async def shutdown(self):
        """Close HTTP session."""
        if self.session:
            await self.session.close()

    async def send_command(
        self,
        target_node: str,
        cmd: Dict[str, Any],
        fallback_mqtt=None
    ) -> Dict[str, Any]:
        """
        Send command to device via HTTP.

        Args:
            target_node: Device ID (e.g., "DEV001")
            cmd: Command payload {cmd_id, action, pin, duration_ms}
            fallback_mqtt: MQTT publisher fallback

        Returns:
            {success, transport, result, error}
        """
        device = self.registry.get_device(target_node)

        if not device:
            return {
                "success": False,
                "transport": "none",
                "error": f"Device {target_node} not found in registry"
            }

        # Check device is reachable
        if device.status == "offline" or not device.ip_address:
            logger.warning(
                f"[HTTP] Device {target_node} offline or no IP. "
                f"Status: {device.status}, IP: {device.ip_address}"
            )

            # Fallback to MQTT for local devices
            if fallback_mqtt:
                logger.info(f"[HTTP] Falling back to MQTT for {target_node}")
                success = await fallback_mqtt.publish_command(target_node, cmd)
                return {
                    "success": success,
                    "transport": "mqtt_fallback",
                    "error": None if success else "MQTT publish failed"
                }

            return {
                "success": False,
                "transport": "none",
                "error": f"Device {target_node} not reachable and no MQTT fallback"
            }

        # Attempt HTTP
        return await self._send_http(target_node, device.ip_address, cmd)

    async def _send_http(
        self,
        target_node: str,
        ip_address: str,
        cmd: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Send command via HTTP with retries."""

        if not self.session:
            return {
                "success": False,
                "transport": "http",
                "error": "HTTP session not initialized"
            }

        url = f"http://{ip_address}:80/api/cmd"

        for attempt in range(self.retries):
            try:
                logger.info(
                    f"[HTTP] Sending command to {target_node} ({ip_address}) "
                    f"attempt {attempt + 1}/{self.retries}"
                )

                async with self.session.post(url, json=cmd) as resp:
                    if resp.status == 200:
                        result = await resp.json()
                        logger.info(
                            f"[HTTP] ✓ {target_node} executed {cmd.get('action')} "
                            f"cmd_id={cmd.get('cmd_id')}"
                        )
                        return {
                            "success": True,
                            "transport": "http",
                            "result": result,
                            "latency_ms": int(
                                (datetime.now().timestamp() -
                                 cmd.get('timestamp', datetime.now().timestamp())) * 1000
                            )
                        }
                    else:
                        logger.warning(
                            f"[HTTP] {target_node} returned {resp.status}: {await resp.text()}"
                        )

            except asyncio.TimeoutError:
                logger.warning(
                    f"[HTTP] Timeout to {target_node} ({ip_address}) "
                    f"attempt {attempt + 1}/{self.retries}"
                )
                if attempt < self.retries - 1:
                    await asyncio.sleep(0.5)  # Brief backoff

            except aiohttp.ClientError as e:
                logger.warning(
                    f"[HTTP] Connection error to {target_node} ({ip_address}): {e} "
                    f"attempt {attempt + 1}/{self.retries}"
                )
                if attempt < self.retries - 1:
                    await asyncio.sleep(0.5)

        return {
            "success": False,
            "transport": "http",
            "error": f"Failed after {self.retries} HTTP attempts to {ip_address}"
        }

    async def is_device_reachable(self, device_id: str) -> bool:
        """Quick health check: can we reach device via HTTP?"""
        device = self.registry.get_device(device_id)
        if not device or not device.ip_address:
            return False

        try:
            url = f"http://{device.ip_address}:80/health"
            async with self.session.get(url, timeout=aiohttp.ClientTimeout(total=2)) as resp:
                return resp.status == 200
        except Exception:
            return False
