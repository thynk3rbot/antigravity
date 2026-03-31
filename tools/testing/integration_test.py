#!/usr/bin/env python3
"""
Unified Integration Test Harness for Magic Firmware
Tests all commands over HTTP and/or MQTT transports.
Generates per-transport coverage reports and wiring verification.
"""

import argparse
import asyncio
import httpx
import time
import json
import yaml
import os
from datetime import datetime
from typing import Dict, List, Tuple
from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.progress import track

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False

console = Console()

class IntegrationTestSuite:
    """Unified test harness for HTTP and MQTT command testing."""

    def __init__(self, config_path: str):
        self.config = self._load_config(config_path)
        self.results = {
            "http": [],
            "mqtt": [],
        }
        self.mqtt_client = None
        self.mqtt_connected = False
        self.response_queue = asyncio.Queue() if MQTT_AVAILABLE else None

    def _load_config(self, config_path: str) -> dict:
        """Load commands.yaml configuration."""
        try:
            with open(config_path, 'r') as f:
                return yaml.safe_load(f)
        except Exception as e:
            console.print(f"[red]Error loading config: {e}[/]")
            raise

    async def _setup_mqtt(self, broker_host: str, broker_port: int) -> bool:
        """Setup MQTT connection."""
        if not MQTT_AVAILABLE:
            console.print("[red]MQTT not available. Install: pip install paho-mqtt[/]")
            return False

        self.mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.mqtt_client.on_connect = self._on_mqtt_connect
        self.mqtt_client.on_message = self._on_mqtt_message
        self.mqtt_client.on_disconnect = self._on_mqtt_disconnect

        try:
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self.mqtt_client.connect, broker_host, broker_port, 60)
            self.mqtt_client.loop_start()

            # Wait for connection
            for _ in range(50):
                if self.mqtt_connected:
                    break
                await asyncio.sleep(0.1)

            if not self.mqtt_connected:
                console.print(f"[red]Failed to connect to MQTT at {broker_host}:{broker_port}[/]")
                return False

            console.print(f"[green]✓ MQTT connected to {broker_host}:{broker_port}[/]")
            return True
        except Exception as e:
            console.print(f"[red]MQTT setup failed: {e}[/]")
            return False

    def _on_mqtt_connect(self, client, userdata, connect_flags, rc, properties):
        if rc == 0:
            self.mqtt_connected = True
            client.subscribe("magic/+/response", qos=1)

    def _on_mqtt_message(self, client, userdata, msg):
        try:
            response = msg.payload.decode('utf-8')
            self.response_queue.put_nowait((True, response))
        except Exception as e:
            self.response_queue.put_nowait((False, str(e)))

    def _on_mqtt_disconnect(self, client, userdata, disconnect_flags, rc, properties):
        self.mqtt_connected = False

    async def _teardown_mqtt(self):
        """Cleanup MQTT connection."""
        if self.mqtt_client:
            try:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
            except:
                pass

    async def test_http(self, target_ip: str, commands: List[str], timeout: float = 5.0) -> List[Dict]:
        """Test commands over HTTP."""
        results = []
        console.print(f"\n[bold cyan]Testing via HTTP → {target_ip}[/bold cyan]")

        async with httpx.AsyncClient() as client:
            for cmd in track(commands, description="HTTP Testing..."):
                start = time.perf_counter()
                try:
                    response = await client.post(
                        f"http://{target_ip}/api/cmd",
                        data={"cmd": cmd},
                        timeout=timeout
                    )
                    elapsed = (time.perf_counter() - start) * 1000

                    if response.status_code == 200:
                        status = "PASS"
                        info = response.text[:200]
                    else:
                        status = "FAIL"
                        info = f"HTTP {response.status_code}"
                except asyncio.TimeoutError:
                    elapsed = (time.perf_counter() - start) * 1000
                    status = "FAIL"
                    info = f"Timeout ({timeout}s)"
                except Exception as e:
                    elapsed = (time.perf_counter() - start) * 1000
                    status = "ERROR"
                    info = str(e)[:100]

                cmd_info = self.config.get("commands", {}).get(cmd, {})
                results.append({
                    "cmd": cmd,
                    "transport": "http",
                    "status": status,
                    "latency_ms": round(elapsed, 2),
                    "info": info,
                    "critical": cmd_info.get("critical", False),
                    "wiring_status": cmd_info.get("wiring_status", "unknown"),
                    "timestamp": datetime.now().isoformat()
                })

        self.results["http"] = results
        return results

    async def test_mqtt(self, broker_host: str, commands: List[str], timeout: float = 5.0) -> List[Dict]:
        """Test commands over MQTT."""
        if not await self._setup_mqtt(broker_host, 1883):
            console.print("[yellow]Skipping MQTT tests (connection failed)[/]")
            return []

        results = []
        console.print(f"\n[bold cyan]Testing via MQTT → {broker_host}:1883[/bold cyan]")

        for cmd in track(commands, description="MQTT Testing..."):
            start = time.perf_counter()
            try:
                # Clear queue
                while not self.response_queue.empty():
                    self.response_queue.get_nowait()

                # Publish command
                self.mqtt_client.publish("magic/cmd", payload=cmd, qos=1)

                # Wait for response
                start_wait = time.time()
                while time.time() - start_wait < timeout:
                    try:
                        ok, response = await asyncio.wait_for(
                            self.response_queue.get(),
                            timeout=0.5
                        )
                        elapsed = (time.perf_counter() - start) * 1000
                        status = "PASS" if ok else "FAIL"
                        info = response[:200]
                        break
                    except asyncio.TimeoutError:
                        continue
                else:
                    elapsed = (time.perf_counter() - start) * 1000
                    status = "FAIL"
                    info = f"MQTT timeout ({timeout}s)"

            except Exception as e:
                elapsed = (time.perf_counter() - start) * 1000
                status = "ERROR"
                info = str(e)[:100]

            cmd_info = self.config.get("commands", {}).get(cmd, {})
            results.append({
                "cmd": cmd,
                "transport": "mqtt",
                "status": status,
                "latency_ms": round(elapsed, 2),
                "info": info,
                "critical": cmd_info.get("critical", False),
                "wiring_status": cmd_info.get("wiring_status", "unknown"),
                "timestamp": datetime.now().isoformat()
            })

        await self._teardown_mqtt()
        self.results["mqtt"] = results
        return results

    def generate_report(self, output_dir: str = "tools/testing/logs"):
        """Generate test report."""
        os.makedirs(output_dir, exist_ok=True)

        # Summary table
        console.print("\n[bold yellow]═══ INTEGRATION TEST SUMMARY ═══[/bold yellow]")

        for transport in ["http", "mqtt"]:
            results = self.results.get(transport, [])
            if not results:
                continue

            table = Table(title=f"{transport.upper()} Results", show_header=True)
            table.add_column("Command", style="cyan")
            table.add_column("Status", justify="center")
            table.add_column("Latency (ms)", justify="right")
            table.add_column("Critical", justify="center")
            table.add_column("Wiring", style="dim")

            passed = failed = 0
            for r in results:
                status_color = {
                    "PASS": "green",
                    "FAIL": "red",
                    "ERROR": "yellow"
                }.get(r["status"], "white")

                table.add_row(
                    r["cmd"],
                    f"[{status_color}]{r['status']}[/{status_color}]",
                    str(r["latency_ms"]),
                    "[red]★[/]" if r["critical"] else " ",
                    r["wiring_status"]
                )

                if r["status"] == "PASS":
                    passed += 1
                elif r["status"] == "FAIL":
                    failed += 1

            console.print(table)
            coverage = (passed / len(results) * 100) if results else 0
            console.print(f"[bold]Coverage: {coverage:.1f}% ({passed}/{len(results)})[/bold]\n")

        # Save JSON report
        report_path = os.path.join(output_dir, f"integration_test_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
        with open(report_path, 'w') as f:
            json.dump(self.results, f, indent=2)

        console.print(f"[green]✓ Report saved to {report_path}[/green]")
        return report_path

    def print_wiring_check(self):
        """Print wiring verification status."""
        console.print("\n[bold yellow]═══ CODEPATH WIRING CHECK ═══[/bold yellow]")

        all_results = self.results.get("http", []) + self.results.get("mqtt", [])
        unimplemented = [
            r for r in all_results
            if r["wiring_status"] not in ["implemented", "partial"]
        ]

        if unimplemented:
            console.print("[red]⚠ Unimplemented commands detected:[/]")
            for r in unimplemented:
                console.print(f"  - {r['cmd']}: {r['wiring_status']}")
        else:
            console.print("[green]✓ All commands are wired up[/]")

async def main():
    parser = argparse.ArgumentParser(description="Magic Integration Test Suite")
    parser.add_argument("--config", type=str, default="tools/testing/commands.yaml", help="Command config (YAML)")
    parser.add_argument("--ip", type=str, default="172.16.0.27", help="HTTP target IP")
    parser.add_argument("--broker", type=str, default="localhost", help="MQTT broker host")
    parser.add_argument("--transports", type=str, default="http,mqtt", help="Comma-separated transports to test")
    parser.add_argument("--critical-only", action="store_true", help="Test critical commands only")
    parser.add_argument("--timeout", type=float, default=5.0, help="Command timeout (seconds)")

    args = parser.parse_args()

    suite = IntegrationTestSuite(args.config)

    # Build command list
    config = suite.config
    if args.critical_only:
        commands = [
            cmd for cmd, info in config.get("commands", {}).items()
            if info.get("critical", False)
        ]
    else:
        commands = list(config.get("commands", {}).keys())

    console.print(f"[bold]Testing {len(commands)} command(s)[/bold]")
    if args.critical_only:
        console.print(f"[dim]CRITICAL ONLY mode[/dim]")

    # Run tests
    transports = [t.strip() for t in args.transports.split(",")]

    if "http" in transports:
        await suite.test_http(args.ip, commands, timeout=args.timeout)

    if "mqtt" in transports:
        await suite.test_mqtt(args.broker, commands, timeout=args.timeout)

    # Generate reports
    suite.generate_report()
    suite.print_wiring_check()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user[/]")
