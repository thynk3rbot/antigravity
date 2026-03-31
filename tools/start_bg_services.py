"""
Magic Background Services Supervisor
Starts all Magic platform services as supervised background processes.
Each service auto-restarts on crash.

Usage:
    python tools/start_bg_services.py          # start all
    python tools/start_bg_services.py stop     # stop all (via PID file)
"""

import subprocess
import os
import sys
import time
import threading

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS = os.path.join(ROOT, "tools")
DAEMON = os.path.join(ROOT, "daemon")
PID_FILE = os.path.join(TOOLS, "bg_services.pid")


def stop_existing():
    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE) as f:
                old_pid = int(f.read().strip())
            subprocess.run(["taskkill", "/PID", str(old_pid), "/F"], capture_output=True)
            print(f"[*] Stopped previous supervisor (PID {old_pid})")
        except Exception:
            pass
        os.remove(PID_FILE)


def write_pid():
    with open(PID_FILE, "w") as f:
        f.write(str(os.getpid()))


def worker(name, cmd, cwd, log_path):
    """Supervised service worker — restarts on crash with 5s backoff."""
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    while True:
        print(f"[{name}] Starting...")
        try:
            with open(log_path, "a") as out:
                out.write(f"\n--- START {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
                p = subprocess.Popen(
                    cmd, cwd=cwd,
                    stdout=out, stderr=subprocess.STDOUT,
                    creationflags=0x08000000  # CREATE_NO_WINDOW on Windows
                )
                p.wait()
                out.write(f"\n--- EXIT code={p.returncode} {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
        except Exception as e:
            print(f"[{name}] Error: {e}")
        print(f"[{name}] Restarting in 5s...")
        time.sleep(5)


SERVICES = [
    {
        "name": "daemon",
        "cmd": [sys.executable, os.path.join(DAEMON, "src", "main.py")],
        "cwd": ROOT,
        "log": os.path.join(ROOT, "logs", "daemon.log"),
    },
    {
        "name": "webapp",
        "cmd": [sys.executable, "server.py"],
        "cwd": os.path.join(TOOLS, "webapp"),
        "log": os.path.join(ROOT, "logs", "webapp.log"),
    },
    {
        "name": "loramsg",
        "cmd": [sys.executable, os.path.join(TOOLS, "loramsg", "server.py")],
        "cwd": ROOT,
        "log": os.path.join(ROOT, "logs", "loramsg.log"),
    },
    {
        "name": "assistant",
        "cmd": [sys.executable, "main.py"],
        "cwd": os.path.join(TOOLS, "assistant"),
        "log": os.path.join(ROOT, "logs", "assistant.log"),
    },
]


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "stop":
        stop_existing()
        print("[*] Done.")
        return

    stop_existing()
    write_pid()

    print()
    print("  Magic Background Services")
    print("  ==========================")
    for s in SERVICES:
        print(f"  {s['name']:<12}  log: logs/{s['name']}.log")
    print()

    threads = []
    for s in SERVICES:
        t = threading.Thread(
            target=worker,
            args=(s["name"], s["cmd"], s["cwd"], s["log"]),
            daemon=True
        )
        t.start()
        threads.append(t)
        time.sleep(1)

    print("[*] All services started. Press Ctrl+C to stop supervisor.")
    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt:
        print("\n[*] Supervisor stopped.")


if __name__ == "__main__":
    main()
