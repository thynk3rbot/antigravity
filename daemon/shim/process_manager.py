# daemon/shim/process_manager.py
import subprocess
import os
import signal
import time
import logging
from typing import Dict, Any, Optional

log = logging.getLogger("daemon.processes")

class ProcessManager:
    def __init__(self):
        self.processes: Dict[str, subprocess.Popen] = {}
        self.stats: Dict[str, Dict[str, Any]] = {}

    def start_plugin(self, name: str, manifest: Dict[str, Any]):
        """Executes a plugin based on its manifest."""
        if name in self.processes:
            log.warning(f"Plugin {name} is already running.")
            return

        run_config = manifest.get("run", {})
        cmd = run_config.get("cmd")
        cwd = manifest.get("path")
        env_file = run_config.get("env_file", ".env")
        
        if not cmd:
            log.error(f"Plugin {name} has no run command.")
            return

        # Prepare environment
        env = os.environ.copy()
        
        # Load .env if it exists
        env_path = os.path.join(cwd, env_file)
        if os.path.exists(env_path):
            log.info(f"Loading environment from {env_path}")
            with open(env_path, "r") as f:
                for line in f:
                    if "=" in line and not line.startswith("#"):
                        key, val = line.strip().split("=", 1)
                        env[key] = val

        # Start process
        try:
            log.info(f"Starting {name}: {cmd} (cwd: {cwd})")
            proc = subprocess.Popen(
                cmd,
                shell=True,
                cwd=cwd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
            )
            self.processes[name] = proc
            self.stats[name] = {
                "start_time": time.time(),
                "restarts": 0,
                "status": "running"
            }
        except Exception as e:
            log.error(f"Failed to start {name}: {e}")

    def stop_plugin(self, name: str):
        """Stops a running plugin."""
        proc = self.processes.get(name)
        if proc:
            log.info(f"Stopping plugin: {name}")
            # Try SIGINT first, then SIGKILL
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                log.warning(f"Plugin {name} did not stop gracefully. Killing.")
                proc.kill()
            
            del self.processes[name]
            if name in self.stats:
                self.stats[name]["status"] = "stopped"

    def stop_all(self):
        """Stops all running plugins."""
        for name in list(self.processes.keys()):
            self.stop_plugin(name)

    def check_health(self):
        """Monitor running processes and restart if needed."""
        for name, proc in list(self.processes.items()):
            # Check if process is still alive
            if proc.poll() is not None:
                log.warning(f"Plugin {name} exited with code {proc.returncode}")
                # TODO: Implement restart policy logic from manifest
                del self.processes[name]
                if name in self.stats:
                    self.stats[name]["status"] = "failed"
                    # Simple auto-restart for now
                    # self.start_plugin(name, manifest_from_registry)
                    
    def get_status(self, name: str) -> str:
        return self.stats.get(name, {}).get("status", "unknown")
