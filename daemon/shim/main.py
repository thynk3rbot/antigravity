# daemon/shim/main.py
import os
import sys
import time
import logging
import signal
import subprocess
from pathlib import Path

# Setup logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log = logging.getLogger("daemon")

from plugin_discovery import PluginDiscovery
from process_manager import ProcessManager

class MagicDaemon:
    def __init__(self, plugins_root: str, infra_file: str):
        self.discovery = PluginDiscovery(plugins_root)
        self.manager = ProcessManager()
        self.infra_file = Path(infra_file)
        self.running = True

    def start_infra(self):
        """Build and start the shared infrastructure stack."""
        if not self.infra_file.exists():
            log.warning(f"Infrastructure file {self.infra_file} not found. Skipping infra boot.")
            return

        log.info(f"Starting infrastructure using {self.infra_file}")
        try:
            # We use subprocess to run docker-compose
            subprocess.run(["docker-compose", "-f", str(self.infra_file), "up", "-d"], check=True)
            log.info("Infrastructure started successfully.")
        except Exception as e:
            log.error(f"Failed to start infrastructure: {e}")

    def boot(self):
        """Full daemon boot sequence."""
        # 1. Start shared infrastructure
        self.start_infra()

        # 2. Discover plugins
        registry = self.discovery.scan()

        # 3. Start auto-start plugins
        for name, manifest in registry.items():
            if manifest.get("auto_start", False):
                self.manager.start_plugin(name, manifest)

        log.info(f"V3 Daemon shim is active. Managed plugins: {len(self.manager.processes)}")

    def run_forever(self):
        """Keep the daemon running and monitor plugins."""
        try:
            while self.running:
                self.manager.check_health()
                time.sleep(5)
        except KeyboardInterrupt:
            self.stop()

    def stop(self, *args):
        log.info("Daemon shutting down...")
        self.running = False
        self.manager.stop_all()
        # Optional: Stop infrastructure? (Usually we leave it up)
        # subprocess.run(["docker-compose", "-f", str(self.infra_file), "down"])
        log.info("Shutdown complete.")
        sys.exit(0)

def main():
    # Paths relative to Antigravity root
    # Project structure:
    # Antigravity/
    #   plugins/
    #     _infrastructure/
    #   daemon/
    #     shim/
    
    root_dir = Path(__file__).parent.parent.parent
    plugins_root = root_dir / "plugins"
    infra_file = root_dir / "plugins" / "_infrastructure" / "docker-compose.yml"
    
    daemon = MagicDaemon(str(plugins_root), str(infra_file))
    
    # Handle signals
    signal.signal(signal.SIGINT, daemon.stop)
    signal.signal(signal.SIGTERM, daemon.stop)
    
    daemon.boot()
    daemon.run_forever()

if __name__ == "__main__":
    main()
