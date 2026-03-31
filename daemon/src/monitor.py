import logging
import os

class MagicMonitor:
    def __init__(self, log_path="c:/temp/magic_cache.log"):
        self.log_path = log_path
        # Ensure directory exists (if possible, handling Windows paths)
        try:
            os.makedirs(os.path.dirname(self.log_path), exist_ok=True)
        except Exception:
            pass
            
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s [%(levelname)s] %(message)s",
            handlers=[
                logging.FileHandler(self.log_path),
                logging.StreamHandler()
            ]
        )
        self.logger = logging.getLogger("MagicCache")
        self.logger.info("--- Magic Cache Monitor Started ---")

    def log_update(self, subject, update_type, keys_count):
        """Log a 'Magic' update (Insert vs Replace)."""
        msg = f"[LVC] {update_type}: Subject={subject}, FieldsUpdated={keys_count}"
        self.logger.info(msg)

    def log_error(self, component, error_msg):
        self.logger.error(f"[{component}] ERROR: {error_msg}")

    def log_info(self, component, msg):
        self.logger.info(f"[{component}] {msg}")

# Global monitor instance
monitor = MagicMonitor()
