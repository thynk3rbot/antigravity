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

async def run_nightly_regression(args):
    target_name = args.name
    transport = args.transport.lower()
    
    console.print(f"[bold cyan]Starting Nightly Test Regime: [yellow]{target_name}[/] via [white]{transport}[/][/bold cyan]")
    if args.target:
        console.print(f"[dim]Routing through gateway to: {args.target} (Response-Aware Verification ACTIVE)[/]")

    async def executor(cmd):
        final_cmd = f"{args.target} {cmd}" if args.target else cmd
        if transport == "http":
            return await http_executor(args.ip, final_cmd, target_node=args.target)
        elif transport == "serial":
            # Serial is harder to poll 'status' unless we send a status command after, 
            # so for now we rely on the line-reader.
            return await serial_executor(args.port, final_cmd)
        else:
            return False, f"Unsupported transport: {transport}"

    engine = TestEngine(executor)
    from testing.engine import DEFAULT_COMMANDS
    results = []
    
    table = Table(title=f"Regression Results - {target_name} ({transport})")
    table.add_column("Command", style="cyan")
    table.add_column("Result", justify="center")
    table.add_column("Latency (ms)")
    table.add_column("Response Snippet", style="dim")

    with Live(table, refresh_per_second=4):
        for cmd in DEFAULT_COMMANDS:
            # We increase the individual command timeout to 15s for mesh polling
            res = await engine.run_single_test(cmd, timeout=18.0)
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
    parser = argparse.ArgumentParser(description="Magic Headless Nightly Test")
    parser.add_argument("--ip", type=str, default="172.16.0.27", help="Target Gateway IP (WiFi)")
    parser.add_argument("--port", type=str, default="COM7", help="Target Serial Port (Headless)")
    parser.add_argument("--transport", type=str, default="http", choices=["http", "serial"], help="Physical transport")
    parser.add_argument("--target", type=str, default=None, help="Routing target (NodeID/MAC) for LoRa/ESPNOW tests")
    parser.add_argument("--name", type=str, default="FleetAdmin", help="Display name for report")
    args = parser.parse_args()
    
    try:
        asyncio.run(run_nightly_regression(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user.[/]")
