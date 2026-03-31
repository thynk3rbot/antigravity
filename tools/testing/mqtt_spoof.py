#!/usr/bin/env python3
"""
MQTT Message Spoofer for Magic Integration Testing
Simulates device responses over MQTT for PC-based testing without daemon.

USER TODO: Replace the placeholder response generation with your actual MQTT client code.
"""

import asyncio
import json
from typing import Dict, List, Optional
import paho.mqtt.client as mqtt
from rich.console import Console

console = Console()

class MQTTMessageSpoofer:
    """
    PC-based MQTT message spoofer for testing firmware commands.
    Publishes commands to the firmware and receives/parses responses.
    """

    def __init__(self, broker_host: str, broker_port: int = 1883, device_id: str = "DEVICE_001"):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.device_id = device_id
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.connected = False
        self.responses: Dict[str, str] = {}

        # Setup callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, connect_flags, rc, properties):
        """MQTT connect callback."""
        if rc == 0:
            self.connected = True
            console.print(f"[green]✓ MQTT Connected[/] (broker: {self.broker_host}:{self.broker_port})")
            # Subscribe to response topics
            client.subscribe(f"magic/{self.device_id}/response", qos=1)
            client.subscribe("magic/+/response", qos=1)
        else:
            console.print(f"[red]✗ MQTT Connection failed: rc={rc}[/]")

    def _on_message(self, client, userdata, msg):
        """MQTT message callback - store responses."""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            self.responses[topic] = payload
            console.print(f"[dim]← {topic}: {payload[:80]}[/]")
        except Exception as e:
            console.print(f"[red]Error processing message: {e}[/]")

    def _on_disconnect(self, client, userdata, disconnect_flags, rc, properties):
        """MQTT disconnect callback."""
        self.connected = False
        if rc != 0:
            console.print(f"[yellow]Unexpected MQTT disconnect: rc={rc}[/]")

    async def connect(self) -> bool:
        """Connect to MQTT broker."""
        try:
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self.client.connect, self.broker_host, self.broker_port, 60)
            self.client.loop_start()

            # Wait for connection
            for _ in range(50):
                if self.connected:
                    return True
                await asyncio.sleep(0.1)

            console.print(f"[red]Failed to connect to {self.broker_host}:{self.broker_port}[/]")
            return False
        except Exception as e:
            console.print(f"[red]Connection error: {e}[/]")
            return False

    async def disconnect(self):
        """Disconnect from MQTT broker."""
        try:
            self.client.loop_stop()
            self.client.disconnect()
        except:
            pass

    async def publish_command(self, cmd: str, args: str = "") -> bool:
        """
        Publish a command to the firmware.

        Args:
            cmd: Command name (e.g., "STATUS", "GPIO", "READ")
            args: Command arguments (e.g., "5 HIGH" for GPIO)

        Returns:
            True if published successfully
        """
        if not self.connected:
            console.print("[red]Not connected to MQTT[/]")
            return False

        try:
            # Publish to command topic
            message = f"{cmd} {args}".strip()
            self.client.publish(f"magic/cmd", payload=message, qos=1)
            console.print(f"[dim]→ magic/cmd: {message}[/]")
            return True
        except Exception as e:
            console.print(f"[red]Publish error: {e}[/]")
            return False

    async def wait_for_response(self, topic_pattern: str, timeout: float = 5.0) -> Optional[str]:
        """
        Wait for a response on a specific topic.

        Args:
            topic_pattern: Topic to listen for (e.g., "magic/*/response")
            timeout: Max wait time in seconds

        Returns:
            Response payload or None if timeout
        """
        start = asyncio.get_event_loop().time()
        while True:
            # Check if we have a response
            for topic, payload in self.responses.items():
                if self._topic_matches(topic, topic_pattern):
                    return payload

            if asyncio.get_event_loop().time() - start > timeout:
                return None

            await asyncio.sleep(0.1)

    @staticmethod
    def _topic_matches(topic: str, pattern: str) -> bool:
        """Simple topic pattern matching (supports +)."""
        # TODO: Implement proper MQTT topic wildcards if needed
        return pattern.replace("+", "*") in topic or topic == pattern

    async def execute_command(self, cmd: str, args: str = "", timeout: float = 5.0) -> Dict:
        """
        Execute a command and return the response.

        Args:
            cmd: Command name
            args: Command arguments
            timeout: Response timeout

        Returns:
            Dict with keys: ok (bool), response (str), latency (float)
        """
        import time
        start = time.perf_counter()

        # Clear old responses
        self.responses.clear()

        # Publish command
        if not await self.publish_command(cmd, args):
            return {"ok": False, "response": "Publish failed", "latency": 0}

        # Wait for response
        response = await self.wait_for_response("magic/+/response", timeout=timeout)

        elapsed = (time.perf_counter() - start) * 1000

        if response is None:
            return {"ok": False, "response": f"Timeout after {timeout}s", "latency": elapsed}

        return {"ok": True, "response": response, "latency": round(elapsed, 2)}


async def demo():
    """Demo: Connect and send a test command."""
    console.print("[bold cyan]MQTT Message Spoofer Demo[/bold cyan]")

    spoofer = MQTTMessageSpoofer("localhost")
    if not await spoofer.connect():
        return

    console.print("[yellow]Sending STATUS command...[/yellow]")
    result = await spoofer.execute_command("STATUS", timeout=5.0)
    console.print(f"[green]Result:[/green] {result}")

    await spoofer.disconnect()

if __name__ == "__main__":
    asyncio.run(demo())
