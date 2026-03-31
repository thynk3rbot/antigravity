"""
Field Configuration Manager — Manage STATUS vs VSTATUS field definitions per device class.

Handles loading, caching, and updating field definitions for dashboard visualization.
Scoped by hardware class (V3, V4) to support hardware-specific metrics.

Industrial deployment best practices:
- Configuration stored in daemon/data/ (persistent, editable at runtime)
- JSON format for human-readability
- Versioned with last_updated timestamp
- Validation on load (all fields have required properties)
- Cache in memory for fast access
"""

import json
import logging
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Any
from filelock import FileLock

logger = logging.getLogger(__name__)

FIELD_DEFINITIONS_PATH = Path(__file__).parent.parent / "field_definitions.json"
LOCK_PATH = Path(__file__).parent.parent / ".field_definitions.lock"


class FieldConfigManager:
    """Manages field definitions for STATUS and VSTATUS display per hardware class."""

    def __init__(self):
        self.config = None
        self.lock = FileLock(str(LOCK_PATH))
        self.load_config()

    def load_config(self) -> Dict[str, Any]:
        """Load field definitions from disk with file locking."""
        try:
            with self.lock:
                if FIELD_DEFINITIONS_PATH.exists():
                    with open(FIELD_DEFINITIONS_PATH, 'r') as f:
                        self.config = json.load(f)
                    logger.info(f"Loaded field definitions from {FIELD_DEFINITIONS_PATH}")
                else:
                    logger.error(f"Field definitions file not found: {FIELD_DEFINITIONS_PATH}")
                    self.config = self._default_config()
            return self.config
        except Exception as e:
            logger.error(f"Failed to load field definitions: {e}")
            self.config = self._default_config()
            return self.config

    def _default_config(self) -> Dict[str, Any]:
        """Return minimal default configuration if file is missing."""
        return {
            "version": "1.0",
            "last_updated": datetime.utcnow().isoformat() + "Z",
            "defaults": {"v3": {}, "v4": {}}
        }

    def get_fields(self, device_class: str, status_type: str) -> List[Dict[str, Any]]:
        """
        Get field definitions for a device class and status type.

        Args:
            device_class: 'v3' or 'v4'
            status_type: 'status' (overview) or 'vstatus' (detailed)

        Returns:
            List of field definitions, sorted by order
        """
        if not self.config:
            self.load_config()

        class_key = device_class.lower()
        if class_key not in self.config.get("defaults", {}):
            logger.warning(f"Unknown device class: {class_key}")
            return []

        status_config = self.config["defaults"][class_key].get(status_type, {})
        fields = status_config.get("fields", [])

        # Sort by order, filter to visible fields
        return sorted(
            [f for f in fields if f.get("visible", True)],
            key=lambda x: x.get("order", 999)
        )

    def get_all_fields(self, device_class: str, status_type: str) -> List[Dict[str, Any]]:
        """Get all fields including hidden ones (for configurator UI)."""
        if not self.config:
            self.load_config()

        class_key = device_class.lower()
        if class_key not in self.config.get("defaults", {}):
            return []

        status_config = self.config["defaults"][class_key].get(status_type, {})
        fields = status_config.get("fields", [])
        return sorted(fields, key=lambda x: x.get("order", 999))

    def get_config(self, device_class: Optional[str] = None) -> Dict[str, Any]:
        """Get entire config or just a device class config."""
        if not self.config:
            self.load_config()

        if device_class:
            class_key = device_class.lower()
            return self.config.get("defaults", {}).get(class_key, {})

        return self.config

    def update_fields(
        self, device_class: str, status_type: str, fields: List[Dict[str, Any]]
    ) -> bool:
        """
        Update field definitions for a device class and status type.

        Args:
            device_class: 'v3' or 'v4'
            status_type: 'status' or 'vstatus'
            fields: List of field definitions

        Returns:
            True if successful, False otherwise
        """
        if not self.config:
            self.load_config()

        try:
            with self.lock:
                class_key = device_class.lower()
                if class_key not in self.config.get("defaults", {}):
                    logger.warning(f"Unknown device class: {class_key}")
                    return False

                # Validate fields
                if not self._validate_fields(fields):
                    logger.error("Field validation failed")
                    return False

                # Update config
                if "defaults" not in self.config:
                    self.config["defaults"] = {}
                if class_key not in self.config["defaults"]:
                    self.config["defaults"][class_key] = {}

                self.config["defaults"][class_key][status_type] = {
                    "display_name": f"{class_key.upper()} {status_type.title()}",
                    "fields": sorted(fields, key=lambda x: x.get("order", 999))
                }

                # Update metadata
                self.config["last_updated"] = datetime.utcnow().isoformat() + "Z"

                # Write to disk
                with open(FIELD_DEFINITIONS_PATH, 'w') as f:
                    json.dump(self.config, f, indent=2)

                logger.info(
                    f"Updated field definitions for {class_key}/{status_type}"
                )
                return True

        except Exception as e:
            logger.error(f"Failed to update field definitions: {e}")
            return False

    def _validate_fields(self, fields: List[Dict[str, Any]]) -> bool:
        """Validate field definition structure."""
        required_keys = {"key", "label", "type", "visible", "order"}

        for field in fields:
            if not isinstance(field, dict):
                logger.error("Field must be a dictionary")
                return False

            if not required_keys.issubset(field.keys()):
                logger.error(f"Field missing required keys: {field}")
                return False

            if not isinstance(field["key"], str) or not field["key"]:
                logger.error(f"Field key must be non-empty string: {field}")
                return False

            if not isinstance(field["visible"], bool):
                logger.error(f"Field visible must be boolean: {field}")
                return False

            if not isinstance(field["order"], int) or field["order"] < 0:
                logger.error(f"Field order must be non-negative integer: {field}")
                return False

        return True

    def reset_to_defaults(self, device_class: Optional[str] = None) -> bool:
        """Reset field definitions to defaults."""
        logger.info(f"Resetting field definitions for {device_class or 'all classes'}")

        try:
            with self.lock:
                if device_class:
                    # Reset only one class
                    class_key = device_class.lower()
                    # Reload from disk to get original defaults
                    with open(FIELD_DEFINITIONS_PATH, 'r') as f:
                        original = json.load(f)
                    if class_key in original.get("defaults", {}):
                        self.config["defaults"][class_key] = original["defaults"][class_key]
                else:
                    # Reload entire config from disk
                    self.load_config()

                self.config["last_updated"] = datetime.utcnow().isoformat() + "Z"
                with open(FIELD_DEFINITIONS_PATH, 'w') as f:
                    json.dump(self.config, f, indent=2)

                logger.info("Field definitions reset successfully")
                return True

        except Exception as e:
            logger.error(f"Failed to reset field definitions: {e}")
            return False


# Global instance
_manager = None


def get_field_config_manager() -> FieldConfigManager:
    """Get or create the global field config manager instance."""
    global _manager
    if _manager is None:
        _manager = FieldConfigManager()
    return _manager
