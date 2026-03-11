import argparse
import asyncio
import httpx
import time
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

console = Console()

async def http_executor(target_ip: str, cmd: str):
    """Simple HTTPX-based executor for the test engine."""
    async with httpx.AsyncClient() as client:
        try:
            # We use a longer timeout for mesh-routed commands
            response = await client.post(
                f"http://{target_ip}/api/cmd",
                data={"cmd": cmd},
                timeout=15.0
            )
            if response.status_code == 200:
                try:
                    res_json = response.json()
                    info = str(res_json.get("response", "OK"))
                except Exception:
                    info = response.text
                return True, info
            else:
                return False, f"HTTP {response.status_code}"
        except Exception as e:
            return False, str(e)

async def serial_executor(port: str, cmd: str, baud=115200):
    """Headless Serial-based executor."""
    if not PYSERIAL:
        return False, "pyserial not installed"
    
    try:
        # We wrap Serial in an executor since it's blocking
        loop = asyncio.get_event_loop()
        def _talk():
            with serial.Serial(port, baud, timeout=5.0) as ser:
                ser.write((cmd + "\n").encode())
                ser.flush()
                # Wait for any response line
                deadline = time.time() + 5.0
                while time.time() < deadline:
                    line = ser.readline().decode(errors='ignore').strip()
                    if line:
                        # Success if we get ANY non-empty line (usually ACK/Response)
                        return True, line
                return False, "Serial Timeout"
        
        return await loop.run_in_executor(None, _talk)
    except Exception as e:
        return False, str(e)

async def run_nightly_regression(args):
    target_name = args.name
    transport = args.transport.lower()
    
    console.print(f"[bold cyan]Starting Nightly Test Regime: [yellow]{target_name}[/] via [white]{transport}[/][/bold cyan]")
    if args.target:
        console.print(f"[dim]Routing through gateway to: {args.target}[/]")

    # 1. Define the executor
    async def executor(cmd):
        # Apply routing prefix if target node is provided
        final_cmd = f"{args.target} {cmd}" if args.target else cmd
        
        if transport == "http":
            return await http_executor(args.ip, final_cmd)
        elif transport == "serial":
            return await serial_executor(args.port, final_cmd)
        else:
            return False, f"Unsupported transport: {transport}"

    engine = TestEngine(executor)
    
    # Run the tests with a live table updates
    from testing.engine import DEFAULT_COMMANDS
    results = []
    
    table = Table(title=f"Regression Results - {target_name} ({transport})")
    table.add_column("Command", style="cyan")
    table.add_column("Result", justify="center")
    table.add_column("Latency (ms)")
    table.add_column("Response Snippet", style="dim")

    with Live(table, refresh_per_second=4):
        for cmd in DEFAULT_COMMANDS:
            res = await engine.run_single_test(cmd, timeout=12.0)
            results.append(res)
            
            status_color = "[green]" if res["status"] == "PASS" else ("[red]" if res["status"] == "FAIL" else "[yellow]")
            table.add_row(
                res["cmd"],
                f"{status_color}{res['status']}[/]",
                f"{res['latency']}",
                res["info"].replace("\n", " ")[:60]
            )
            await asyncio.sleep(0.5)

    passed = len([r for r in results if r["status"] == "PASS"])
    total = len(results)
    
    if passed == total:
        console.print(f"\n[bold green]PASSED: ALL {total} TESTS PASSED[/bold green]")
    else:
        console.print(f"\n[bold red]FAILED: REGRESSION DETECTED: {total - passed} failures[/bold red]")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="LoRaLink Headless Nightly Test")
    parser.add_argument("--ip", type=str, default="172.16.0.42", help="Target Gateway IP (WiFi)")
    parser.add_argument("--port", type=str, default="COM3", help="Target Serial Port (Headless)")
    parser.add_argument("--transport", type=str, default="http", choices=["http", "serial"], help="Physical transport")
    parser.add_argument("--target", type=str, default=None, help="Routing target (NodeID/MAC) for LoRa/ESPNOW tests")
    parser.add_argument("--name", type=str, default="FleetAdmin", help="Display name for report")
    args = parser.parse_args()
    
    try:
        asyncio.run(run_nightly_regression(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user.[/]")
