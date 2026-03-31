"""
Deployment Configuration Manager
Loads and manages feature flags based on deployment mode (factory, user, manager, homeowner).
"""

import os
import yaml
import logging
from pathlib import Path
from typing import Dict, List, Optional

logger = logging.getLogger(__name__)

class DeploymentConfig:
    """
    Manages feature flags and API access based on deployment mode.
    """

    def __init__(self, config_path: Path = None, mode: Optional[str] = None):
        self.config_path = config_path or Path("daemon/config/deployments.yaml")
        self.mode = mode or os.getenv("DEPLOYMENT_MODE", "factory")
        self.config = self._load_config()
        self.deployment = self.config.get("deployments", {}).get(self.mode)

        if not self.deployment:
            raise ValueError(f"Unknown deployment mode: {self.mode}. Available: {list(self.config.get('deployments', {}).keys())}")

        logger.info(f"Deployment: {self.mode} - {self.deployment.get('description')}")

    def _load_config(self) -> Dict:
        """Load deployments.yaml configuration."""
        try:
            with open(self.config_path, 'r') as f:
                return yaml.safe_load(f)
        except Exception as e:
            logger.error(f"Failed to load deployment config: {e}")
            raise

    def is_feature_enabled(self, feature: str) -> bool:
        """Check if a feature is enabled in this deployment."""
        return self.deployment.get("features", {}).get(feature, False)

    def get_enabled_features(self) -> List[str]:
        """Get list of all enabled features."""
        return [k for k, v in self.deployment.get("features", {}).items() if v]

    def get_allowed_roles(self) -> List[str]:
        """Get list of allowed roles in this deployment."""
        return self.deployment.get("roles", [])

    def get_enabled_panels(self) -> List[str]:
        """Get UI panels to display."""
        return self.deployment.get("ui", {}).get("panels", [])

    def get_default_view(self) -> str:
        """Get default dashboard view."""
        return self.deployment.get("ui", {}).get("default_view", "status")

    def get_theme(self) -> str:
        """Get UI theme (light/dark)."""
        return self.deployment.get("ui", {}).get("theme", "dark")

    def get_sidebar_level(self) -> str:
        """Get sidebar complexity level: full, moderate, minimal."""
        return self.deployment.get("ui", {}).get("sidebar", "full")

    def can_access_registry(self) -> bool:
        """Can user access device registry."""
        access = self.deployment.get("api_access", {}).get("registry", "none")
        return access in ["full", "read-only"]

    def can_manage_registry(self) -> bool:
        """Can user create/edit/delete devices."""
        access = self.deployment.get("api_access", {}).get("registry", "none")
        return access == "full"

    def can_flash_ota(self) -> bool:
        """Can user initiate OTA flashing."""
        access = self.deployment.get("api_access", {}).get("ota", "none")
        return access in ["full", "swarm-only"]

    def can_flash_usb(self) -> bool:
        """Can user flash via USB (virgin devices)."""
        return self.is_feature_enabled("usb_flashing")

    def get_ota_access_level(self) -> str:
        """Get OTA access level: full, swarm-only, none."""
        return self.deployment.get("api_access", {}).get("ota", "none")

    def get_diagnostics_access(self) -> str:
        """Get diagnostics access level: full, minimal, none."""
        return self.deployment.get("api_access", {}).get("diagnostics", "none")

    def to_json(self) -> Dict:
        """Export config as JSON for frontend."""
        return {
            "mode": self.mode,
            "description": self.deployment.get("description"),
            "features": self.deployment.get("features", {}),
            "ui": {
                "sidebar": self.get_sidebar_level(),
                "panels": self.get_enabled_panels(),
                "theme": self.get_theme(),
                "default_view": self.get_default_view(),
            },
            "api_access": self.deployment.get("api_access", {}),
        }


# Singleton instance
_instance: Optional[DeploymentConfig] = None

def init_deployment_config(config_path: Path = None, mode: Optional[str] = None) -> DeploymentConfig:
    """Initialize the global deployment config."""
    global _instance
    _instance = DeploymentConfig(config_path, mode)
    return _instance

def get_deployment_config() -> DeploymentConfig:
    """Get the global deployment config instance."""
    global _instance
    if _instance is None:
        _instance = DeploymentConfig()
    return _instance
