import argparse
import asyncio
import httpx
import time
import yaml
from rich.console import Console
from rich.table import Table
from rich.live import Live
from testing.engine import TestEngine

# Pyserial for headless serial transport
try:
    import serial
    PYSERIAL = True
except ImportError:
    PYSERIAL = False

# MQTT support (optional, requires paho-mqtt)
try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False

console = Console()

def load_command_config(config_path: str) -> dict:
    """Load commands.yaml config file."""
    try:
        with open(config_path, 'r') as f:
            return yaml.safe_load(f)
    except Exception as e:
        console.print(f"[yellow]Warning: Could not load {config_path}: {e}[/]")
        return {"commands": {}, "test_config": {}}

async def http_executor(target_ip: str, cmd: str, target_node: str = None):
    """
    HTTPX-based executor with Mesh-Aware response polling.
    If target_node is provided, it polls /api/status until last_cmd matches.
    """
    async with httpx.AsyncClient() as client:
        prev_resp = ""
        if target_node:
            try:
                r = await client.get(f"http://{target_ip}/api/status", timeout=2.0)
                if r.status_code == 200:
                    prev_resp = r.json().get("last_cmd", "")
            except Exception:
                pass

        try:
            # Send the command
            response = await client.post(
                f"http://{target_ip}/api/cmd",
                data={"cmd": cmd},
                timeout=5.0
            )
            if response.status_code != 200:
                return False, f"HTTP {response.status_code}"
            
            # If it's local, we just return the immediate response
            if not target_node:
                return True, response.text.strip() or "OK"
            
            # If it's mesh, we poll the status for the real response from the edge
            start_poll = time.time()
            deadline = 12.0 # LoRa mesh timeout
            while time.time() - start_poll < deadline:
                try:
                    r = await client.get(f"http://{target_ip}/api/status", timeout=2.0)
                    if r.status_code == 200:
                        curr_resp = r.json().get("last_cmd", "")
                        # Verify it's a NEW message and belongs to our target node
                        if curr_resp != prev_resp and f"[{target_node}]" in curr_resp:
                            return True, curr_resp
                except Exception:
                    pass
                await asyncio.sleep(0.8) # Wait for LoRa cycle
            
            return False, "Mesh Timeout (Gateway OK, Node Idle)"

        except Exception as e:
            return False, str(e)

async def serial_executor(port: str, cmd: str, baud=115200):
    """Headless Serial-based executor."""
    if not PYSERIAL:
        return False, "pyserial not installed"

    try:
        loop = asyncio.get_event_loop()
        def _talk():
            with serial.Serial(port, baud, timeout=5.0) as ser:
                ser.write((cmd + "\n").encode())
                ser.flush()
                deadline = time.time() + 8.0
                while time.time() < deadline:
                    line = ser.readline().decode(errors='ignore').strip()
                    if line:
                        return True, line
                return False, "Serial Timeout"

        return await loop.run_in_executor(None, _talk)
    except Exception as e:
        return False, str(e)

class MQTTExecutor:
    """MQTT-based executor for command testing."""
    def __init__(self, broker_host: str, broker_port: int = 1883):
        if not MQTT_AVAILABLE:
            raise RuntimeError("paho-mqtt not installed. Install with: pip install paho-mqtt")

        self.broker_host = broker_host
        self.broker_port = broker_port
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.response_queue = asyncio.Queue()
        self.connected = False

        # Set callbacks
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, connect_flags, rc, properties):
        """MQTT connect callback."""
        if rc == 0:
            self.connected = True
            # Subscribe to response topic
            client.subscribe("magic/+/response", qos=1)
        else:
            self.connected = False

    def _on_message(self, client, userdata, msg):
        """MQTT message callback."""
        try:
            response = msg.payload.decode('utf-8')
            # Put response in queue for polling
            self.response_queue.put_nowait((True, response))
        except Exception as e:
            self.response_queue.put_nowait((False, str(e)))

    def _on_disconnect(self, client, userdata, disconnect_flags, rc, properties):
        """MQTT disconnect callback."""
        self.connected = False

    async def connect(self):
        """Connect to MQTT broker."""
        try:
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self.client.connect, self.broker_host, self.broker_port, 60)
            self.client.loop_start()
            # Wait for connection
            for _ in range(50):  # 5 seconds max
                if self.connected:
                    break
                await asyncio.sleep(0.1)
            return self.connected
        except Exception as e:
            console.print(f"[red]MQTT connection failed: {e}[/]")
            return False

    async def disconnect(self):
        """Disconnect from MQTT broker."""
        try:
            self.client.loop_stop()
            self.client.disconnect()
        except:
            pass

    async def execute_command(self, cmd: str, timeout: float = 5.0) -> tuple[bool, str]:
        """Send command via MQTT and wait for response."""
        if not self.connected:
            return False, "MQTT not connected"

        try:
            # Clear old responses
            while not self.response_queue.empty():
                self.response_queue.get_nowait()

            # Publish command to magic/cmd topic
            self.client.publish("magic/cmd", payload=cmd, qos=1)

            # Wait for response with timeout
            start_time = time.time()
            while time.time() - start_time < timeout:
                try:
                    ok, response = await asyncio.wait_for(
                        self.response_queue.get(),
                        timeout=0.5
                    )
                    return ok, response
                except asyncio.TimeoutError:
                    continue

            return False, f"MQTT timeout after {timeout}s"
        except Exception as e:
            return False, str(e)

async def mqtt_executor(broker_host: str, cmd: str, mqtt_executor_instance=None):
    """MQTT executor wrapper."""
    if not mqtt_executor_instance:
        return False, "MQTT executor not initialized"

    return await mqtt_executor_instance.execute_command(cmd)

async def run_nightly_regression(args):
    target_name = args.name
    transport = args.transport.lower()
    config = load_command_config(args.config)

    console.print(f"[bold cyan]Starting Nightly Test Regime: [yellow]{target_name}[/] via [white]{transport}[/][/bold cyan]")
    if args.target:
        console.print(f"[dim]Routing through gateway to: {args.target} (Response-Aware Verification ACTIVE)[/]")
    if args.critical_only:
        console.print(f"[dim]Testing CRITICAL commands only[/]")

    # Initialize MQTT if needed
    mqtt_inst = None
    if transport == "mqtt":
        if not MQTT_AVAILABLE:
            console.print("[red]Error: MQTT transport selected but paho-mqtt not installed[/]")
            console.print("[yellow]Install with: pip install paho-mqtt[/]")
            return

        mqtt_inst = MQTTExecutor(args.broker_host, args.broker_port)
        connected = await mqtt_inst.connect()
        if not connected:
            console.print(f"[red]Failed to connect to MQTT broker at {args.broker_host}:{args.broker_port}[/]")
            return
        console.print(f"[green]Connected to MQTT broker at {args.broker_host}:{args.broker_port}[/]")

    async def executor(cmd):
        final_cmd = f"{args.target} {cmd}" if args.target else cmd
        if transport == "http":
            return await http_executor(args.ip, final_cmd, target_node=args.target)
        elif transport == "serial":
            return await serial_executor(args.port, final_cmd)
        elif transport == "mqtt":
            return await mqtt_executor(args.broker_host, final_cmd, mqtt_inst)
        else:
            return False, f"Unsupported transport: {transport}"

    engine = TestEngine(executor)

    # Build command list from config
    commands_to_test = []
    if args.critical_only:
        # Test only critical commands
        for cmd_name, cmd_info in config.get("commands", {}).items():
            if cmd_info.get("critical", False):
                commands_to_test.append(cmd_name)
    elif args.commands:
        # Test specific commands from command line
        commands_to_test = args.commands.split(",")
    else:
        # Test all commands
        commands_to_test = list(config.get("commands", {}).keys())

    if not commands_to_test:
        # Fallback to legacy defaults
        from testing.engine import DEFAULT_COMMANDS
        commands_to_test = DEFAULT_COMMANDS

    results = []

    table = Table(title=f"Regression Results - {target_name} ({transport})")
    table.add_column("Command", style="cyan")
    table.add_column("Result", justify="center")
    table.add_column("Latency (ms)")
    table.add_column("Response Snippet", style="dim")

    with Live(table, refresh_per_second=4):
        for cmd in commands_to_test:
            # Use config timeout or default
            test_config = config.get("test_config", {})
            timeout = float(test_config.get("mesh_timeout", 18.0))

            res = await engine.run_single_test(cmd, timeout=timeout)
            results.append(res)

            status_color = "[green]" if res["status"] == "PASS" else ("[red]" if res["status"] == "FAIL" else "[yellow]")
            table.add_row(
                res["cmd"],
                f"{status_color}{res['status']}[/]",
                f"{res['latency']}",
                res["info"].replace("\n", " ")[:60]
            )
            await asyncio.sleep(0.5)

    # Cleanup
    if mqtt_inst:
        await mqtt_inst.disconnect()

    passed = len([r for r in results if r["status"] == "PASS"])
    total = len(results)

    if passed == total:
        console.print(f"\n[bold green]PASSED: ALL {total} TESTS PASSED[/bold green]")
    else:
        console.print(f"\n[bold red]FAILED: {total - passed}/{total} failures[/bold red]")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Magic Headless Nightly Test (with MQTT support)")
    parser.add_argument("--ip", type=str, default="172.16.0.27", help="Target Gateway IP (HTTP)")
    parser.add_argument("--port", type=str, default="COM7", help="Target Serial Port (Headless)")
    parser.add_argument("--transport", type=str, default="http", choices=["http", "serial", "mqtt"], help="Physical transport")
    parser.add_argument("--broker-host", type=str, default="localhost", help="MQTT broker host (for --transport mqtt)")
    parser.add_argument("--broker-port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--target", type=str, default=None, help="Routing target (NodeID/MAC) for LoRa/ESPNOW tests")
    parser.add_argument("--name", type=str, default="FleetAdmin", help="Display name for report")
    parser.add_argument("--config", type=str, default="tools/testing/commands.yaml", help="Command config file (YAML)")
    parser.add_argument("--critical-only", action="store_true", help="Test critical commands only")
    parser.add_argument("--commands", type=str, default=None, help="Comma-separated list of commands to test")
    args = parser.parse_args()

    try:
        asyncio.run(run_nightly_regression(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user.[/]")
