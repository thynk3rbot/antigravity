import argparse
import asyncio
import httpx
import time
import json
import os
import yaml
from datetime import datetime
from rich.console import Console
from rich.table import Table
from rich.live import Live
from engine import TestEngine, DEFAULT_COMMANDS

# MQTT support
try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False

console = Console()

LOG_DIR = os.path.join(os.path.dirname(__file__), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

def load_command_config(config_path: str) -> dict:
    """Load commands.yaml config file."""
    try:
        with open(config_path, 'r') as f:
            return yaml.safe_load(f)
    except Exception as e:
        console.print(f"[yellow]Warning: Could not load {config_path}: {e}[/]")
        return {"commands": {}, "test_config": {}}

async def http_executor(target_ip: str, cmd: str, target_node: str = None):
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
            response = await client.post(
                f"http://{target_ip}/api/cmd",
                json={"cmd": cmd},
                timeout=5.0
            )
            if response.status_code != 200:
                return False, f"HTTP {response.status_code}"
            
            if not target_node:
                return True, response.text.strip() or "OK"
            
            start_poll = time.time()
            deadline = 15.0 # extended LoRa mesh timeout
            while time.time() - start_poll < deadline:
                try:
                    r = await client.get(f"http://{target_ip}/api/status", timeout=2.0)
                    if r.status_code == 200:
                        curr_resp = r.json().get("last_cmd", "")
                        if curr_resp != prev_resp and f"[{target_node}]" in curr_resp:
                            return True, curr_resp
                except Exception:
                    pass
                await asyncio.sleep(0.8)
            
            return False, "Mesh Timeout (Gateway OK, Node Idle)"

        except Exception as e:
            return False, str(e)

def analyze_failures(results):
    """Generate a markdown To-Do list based on test failures."""
    failures = [r for r in results if r["status"] != "PASS"]
    if not failures:
        return "All systems operational. No immediate fixes required."

    todo_content = f"# Nightly Overdrive Fixes - {datetime.now().strftime('%Y-%m-%d')}\n\n"
    todo_content += "The following commands failed during the overnight endurance test:\n\n"
    
    for f in failures:
        todo_content += f"- [ ] **[Investigate]** Command `{f['cmd']}` failed.\n"
        todo_content += f"  - **Error:** {f['info']}\n"
        todo_content += f"  - **Latency recorded before failure:** {f['latency']}ms\n"

    return todo_content

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

        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, connect_flags, rc, properties):
        if rc == 0:
            self.connected = True
            client.subscribe("magic/+/response", qos=1)
        else:
            self.connected = False

    def _on_message(self, client, userdata, msg):
        try:
            response = msg.payload.decode('utf-8')
            self.response_queue.put_nowait((True, response))
        except Exception as e:
            self.response_queue.put_nowait((False, str(e)))

    def _on_disconnect(self, client, userdata, disconnect_flags, rc, properties):
        self.connected = False

    async def connect(self):
        try:
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self.client.connect, self.broker_host, self.broker_port, 60)
            self.client.loop_start()
            for _ in range(50):
                if self.connected:
                    break
                await asyncio.sleep(0.1)
            return self.connected
        except Exception as e:
            console.print(f"[red]MQTT connection failed: {e}[/]")
            return False

    async def disconnect(self):
        try:
            self.client.loop_stop()
            self.client.disconnect()
        except:
            pass

    async def execute_command(self, cmd: str, timeout: float = 5.0) -> tuple[bool, str]:
        if not self.connected:
            return False, "MQTT not connected"

        try:
            while not self.response_queue.empty():
                self.response_queue.get_nowait()

            self.client.publish("magic/cmd", payload=cmd, qos=1)

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
    if not mqtt_executor_instance:
        return False, "MQTT executor not initialized"
    return await mqtt_executor_instance.execute_command(cmd)

async def run_overdrive(args):
    target_name = args.name
    cycles = args.cycles
    delay = args.delay
    transport = args.transport.lower()
    config = load_command_config(args.config)

    console.print(f"[bold red]Initiating Nightly Overdrive on [yellow]{target_name}[/] via [white]{transport}[/][/bold red]")
    console.print(f"[dim]Running {cycles} cycles with {delay}s delay between loops[/]")
    if args.critical_only:
        console.print(f"[dim]Testing CRITICAL commands only[/]")

    # Initialize MQTT if needed
    mqtt_inst = None
    if transport == "mqtt":
        if not MQTT_AVAILABLE:
            console.print("[red]Error: MQTT transport selected but paho-mqtt not installed[/]")
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
        elif transport == "mqtt":
            return await mqtt_executor(args.broker_host, final_cmd, mqtt_inst)
        else:
            return False, f"Unsupported transport: {transport}"

    engine = TestEngine(executor)

    # Build command list from config
    commands_to_test = []
    if args.critical_only:
        for cmd_name, cmd_info in config.get("commands", {}).items():
            if cmd_info.get("critical", False):
                commands_to_test.append(cmd_name)
    else:
        commands_to_test = list(config.get("commands", {}).keys())

    if not commands_to_test:
        commands_to_test = DEFAULT_COMMANDS

    all_results = []
    start_time = time.time()

    for i in range(1, cycles + 1):
        console.print(f"--- Cycle {i}/{cycles} ---")
        test_config = config.get("test_config", {})
        timeout = float(test_config.get("mesh_timeout", 15.0))
        cycle_results = await engine.run_suite(commands_to_test, delay=0.5, timeout_per_cmd=timeout)

        for r in cycle_results:
            r['cycle'] = i
            r['timestamp_iso'] = datetime.now().isoformat()
            # Track if command is critical
            cmd_info = config.get("commands", {}).get(r['cmd'], {})
            r['critical'] = cmd_info.get("critical", False)

            color = "green" if r["status"] == "PASS" else "red" if r["status"] == "FAIL" else "yellow"
            critical_badge = "[red]★[/]" if r.get('critical') else " "
            console.print(f"{critical_badge} [{color}]{r['status']}[/] | {r['cmd']} | {r['latency']}ms")

        all_results.extend(cycle_results)

        if i < cycles:
            await asyncio.sleep(delay)

    end_time = time.time()

    # Save the raw data log
    log_name = f"overdrive_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    log_path = os.path.join(LOG_DIR, log_name)
    with open(log_path, 'w') as f:
        json.dump({
            "target": target_name,
            "ip": args.ip,
            "transport": transport,
            "mesh_target": args.target,
            "critical_only": args.critical_only,
            "cycles": cycles,
            "duration_sec": round(end_time - start_time, 2),
            "results": all_results
        }, f, indent=4)

    console.print(f"\n[bold green]Report saved to {log_path}[/]")

    # Generate Todo list from failures
    todo_name = f"TODO_{datetime.now().strftime('%Y%m%d')}.md"
    todo_path = os.path.join(LOG_DIR, todo_name)
    todo_content = analyze_failures(all_results)
    
    with open(todo_path, 'w') as f:
        f.write(todo_content)
        
    console.print(f"[bold cyan]Generated Action Items: {todo_path}[/]")

    # Auto-generate graphical trend report
    try:
        from generate_report import generate_html_report
        report_path = generate_html_report(log_path)
        console.print(f"\n[bold magenta]View Interactive Dashboard at:[/] {report_path}")
    except Exception as e:
        console.print(f"[red]Failed to generate dashboard: {e}[/]")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Magic Overnight Endurance Engine (with MQTT support)")
    parser.add_argument("--ip", type=str, default="172.16.0.27", help="Gateway IP (for HTTP transport)")
    parser.add_argument("--transport", type=str, default="http", choices=["http", "mqtt"], help="Transport protocol")
    parser.add_argument("--broker-host", type=str, default="localhost", help="MQTT broker host (for --transport mqtt)")
    parser.add_argument("--broker-port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--target", type=str, default=None, help="LoRa Mesh Target Node ID")
    parser.add_argument("--name", type=str, default="LocalNet", help="Target Name")
    parser.add_argument("--cycles", type=int, default=5, help="Number of complete test cycles to run")
    parser.add_argument("--delay", type=float, default=60.0, help="Delay in seconds between cycles")
    parser.add_argument("--config", type=str, default="tools/testing/commands.yaml", help="Command config file (YAML)")
    parser.add_argument("--critical-only", action="store_true", help="Test critical commands only")
    args = parser.parse_args()

    try:
        asyncio.run(run_overdrive(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user.[/]")
