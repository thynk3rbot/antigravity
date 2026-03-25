import logging
from pathlib import Path
import json
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager
from tools.daemon.api import create_api_app

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(levelname)s: %(message)s")
logger = logging.getLogger(__name__)

DEFAULT_CONFIG = {
    "host": "0.0.0.0",
    "port": 8001,
    "db_path": "daemon_queue.db",
    "lifecycle_mode": "service",
    "start_on_boot": True
}


class LoRaLinkDaemon:
    """Main LoRaLink PC Daemon service.

    Wires together: SQLite persistence + transport routing + FastAPI REST API.
    """

    def __init__(self, config_path: Path):
        self.config_path = config_path
        self.config = self._load_config()
        logger.info(f"Daemon config loaded from {config_path}")

        # Initialize components
        self.message_queue = MessageQueue(Path(self.config["db_path"]))
        self.transport_manager = TransportManager()
        self.api_app = create_api_app(self.message_queue, self.transport_manager)
        logger.info("Daemon components initialized")

    def _load_config(self) -> dict:
        """Load daemon configuration from file, falling back to defaults."""
        if self.config_path.exists():
            try:
                loaded = json.loads(self.config_path.read_text())
                # Merge with defaults so new keys always have values
                return {**DEFAULT_CONFIG, **loaded}
            except (json.JSONDecodeError, OSError) as e:
                logger.warning(f"Failed to load config {self.config_path}: {e}. Using defaults.")
        return DEFAULT_CONFIG.copy()

    def run(self):
        """Start the daemon service (blocking)."""
        try:
            import uvicorn
        except ImportError:
            logger.error("uvicorn not installed. Run: pip install uvicorn")
            raise

        host = self.config["host"]
        port = self.config["port"]
        logger.info(f"Starting LoRaLink Daemon on {host}:{port}")

        uvicorn.run(
            self.api_app,
            host=host,
            port=port,
            log_level="info"
        )


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="LoRaLink PC Daemon")
    parser.add_argument("--config", default="daemon.config.json", help="Config file path")
    args = parser.parse_args()

    daemon = LoRaLinkDaemon(Path(args.config))
    daemon.run()
