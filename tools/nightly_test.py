import argparse
import time
import requests
import json
from rich.console import Console
from rich.table import Table

console = Console()

# Defined command groups to test
COMMANDS = [
    # Health & System
    "STATUS",
    "RADIO",
    "TASKS",
    "UPTIME",
    # Mesh Network
    "NODES",
    # GPIO & Sensors
    "READ LED",
    "READ 6",
    # LED Control
    "LED ON",
    "LED OFF",
    "BLINK",
    # Scheduler
    "SCHED LIST",
]

def run_tests(target_ip, target_name):
    console.print(f"[bold cyan]Starting Nightly Test Regime for {target_name} ({target_ip})[/bold cyan]")
    
    results = []
    
    for cmd in COMMANDS:
        console.print(f"[dim]Sending command: {cmd}[/dim]")
        start_time = time.time()
        
        try:
            # Send command via the Fleet Admin local webapp proxy or direct HTTP if available
            response = requests.post(
                f"http://{target_ip}/api/cmd", 
                json={"cmd": cmd},
                timeout=5.0
            )
            elapsed = (time.time() - start_time) * 1000
            
            if response.status_code == 200:
                try:
                    res_json = response.json()
                    status = "[green]PASS[/green]"
                    info = str(res_json.get("response", "OK"))[:50]
                except json.JSONDecodeError:
                    status = "[green]PASS (Text)[/green]"
                    info = response.text[:50]
                    
                results.append((cmd, status, f"{elapsed:.1f}ms", info.replace("\n", " ")))
            else:
                results.append((cmd, "[red]FAIL[/red]", f"{elapsed:.1f}ms", f"HTTP {response.status_code}"))
        except requests.exceptions.RequestException as e:
            elapsed = (time.time() - start_time) * 1000
            results.append((cmd, "[red]ERROR[/red]", f"{elapsed:.1f}ms", str(e)))
            
        time.sleep(1.0) # pacing
        
    # Render results table
    table = Table(title=f"Nightly Test Results - {target_name}")
    table.add_column("Command", style="cyan")
    table.add_column("Result", justify="center")
    table.add_column("Latency")
    table.add_column("Response Snippet", style="dim")
    
    for row in results:
        table.add_row(*row)
        
    console.print(table)
    
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Nightly Test Regime")
    parser.add_argument("--ip", type=str, default="127.0.0.1:8000", help="IP address of the device or Fleet Admin")
    parser.add_argument("--name", type=str, default="FleetAdmin", help="Name of the target for reporting")
    args = parser.parse_args()
    
    run_tests(args.ip, args.name)
