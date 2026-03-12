import subprocess
import os
import sys
import time
import threading

def start_docker(repo_dir):
    CREATE_NO_WINDOW = 0x08000000
    try:
        print("Starting MQTT Broker (Docker)...")
        subprocess.run(
            ["docker", "compose", "-f", "mqttdocker.yml", "up", "-d"],
            cwd=repo_dir,
            creationflags=CREATE_NO_WINDOW
        )
    except Exception as e:
        print(f"Failed to start docker compose: {e}")

def worker(cmd, cwd, log_file):
    CREATE_NO_WINDOW = 0x08000000
    while True:
        try:
            with open(log_file, "a") as out:
                out.write("\n--- STARTING SERVICE ---\n")
                p = subprocess.Popen(
                    cmd,
                    cwd=cwd,
                    creationflags=CREATE_NO_WINDOW,
                    stdout=out,
                    stderr=subprocess.STDOUT
                )
                p.wait()
                out.write(f"\n--- STOPPED with code {p.returncode} ---\n")
        except Exception as e:
            with open(log_file, "a") as out:
                out.write(f"\n--- ERROR: {e} ---\n")
        time.sleep(5)

def write_pid(pid_file):
    with open(pid_file, "w") as f:
        f.write(str(os.getpid()))

def main():
    repo_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    tools_dir = os.path.join(repo_dir, "tools")
    
    # Check if already running
    pid_file = os.path.join(tools_dir, "bg_services.pid")
    if os.path.exists(pid_file):
        try:
            with open(pid_file, "r") as f:
                old_pid = int(f.read().strip())
            # Basic process check (Windows)
            subprocess.run(["taskkill", "/PID", str(old_pid), "/F"], capture_output=True)
        except:
            pass
            
    write_pid(pid_file)

    start_docker(repo_dir)

    services = [
        {
            "cmd": [sys.executable, "-m", "http.server", "8001", "-d", "docs"],
            "cwd": repo_dir,
            "log": os.path.join(repo_dir, "docs_startup.log")
        },
        {
            "cmd": [sys.executable, "server.py"],
            "cwd": os.path.join(tools_dir, "webapp"),
            "log": os.path.join(tools_dir, "webapp", "server_out.log")
        },
        {
            "cmd": [sys.executable, "server.py"],
            "cwd": os.path.join(tools_dir, "website"),
            "log": os.path.join(tools_dir, "website", "server_out.log")
        }
    ]

    for s in services:
        t = threading.Thread(target=worker, args=(s["cmd"], s["cwd"], s["log"]), daemon=True)
        t.start()
        time.sleep(1) # Stagger startups slightly

    print("All services started in background successfully.")
    
    # Stay alive to supervise
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    except Exception:
        pass

if __name__ == '__main__':
    main()
