import argparse
import asyncio
import httpx
import time
import json
import os
from datetime import datetime
from rich.console import Console
from rich.table import Table
from rich.live import Live
from engine import TestEngine, DEFAULT_COMMANDS

console = Console()

LOG_DIR = os.path.join(os.path.dirname(__file__), "logs")
os.makedirs(LOG_DIR, exist_ok=True)

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

async def run_overdrive(args):
    target_name = args.name
    cycles = args.cycles
    delay = args.delay

    console.print(f"[bold red]Initiating Nightly Overdrive on [yellow]{target_name}[/][/bold red]")
    console.print(f"[dim]Running {cycles} cycles with {delay}s delay between loops[/]")
    
    async def executor(cmd):
        final_cmd = f"{args.target} {cmd}" if args.target else cmd
        return await http_executor(args.ip, final_cmd, target_node=args.target)

    engine = TestEngine(executor)
    
    all_results = []
    
    start_time = time.time()

    for i in range(1, cycles + 1):
        console.print(f"--- Cycle {i}/{cycles} ---")
        cycle_results = await engine.run_suite(DEFAULT_COMMANDS, delay=0.5, timeout_per_cmd=15.0)
        
        for r in cycle_results:
            r['cycle'] = i
            r['timestamp_iso'] = datetime.now().isoformat()
            
            color = "green" if r["status"] == "PASS" else "red" if r["status"] == "FAIL" else "yellow"
            console.print(f"[{color}]{r['status']}[/] | {r['cmd']} | {r['latency']}ms")
            
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
            "mesh_target": args.target,
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
    parser = argparse.ArgumentParser(description="LoRaLink Overnight Endurance Engine")
    parser.add_argument("--ip", type=str, default="172.16.0.27", help="Gateway IP")
    parser.add_argument("--target", type=str, default=None, help="LoRa Mesh Target Node ID")
    parser.add_argument("--name", type=str, default="LocalNet", help="Target Name")
    parser.add_argument("--cycles", type=int, default=5, help="Number of complete test cycles to run")
    parser.add_argument("--delay", type=float, default=60.0, help="Delay in seconds between cycles")
    args = parser.parse_args()
    
    try:
        asyncio.run(run_overdrive(args))
    except KeyboardInterrupt:
        console.print("\n[yellow]Test interrupted by user.[/]")
