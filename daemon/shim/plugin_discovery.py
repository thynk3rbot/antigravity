# daemon/shim/plugin_discovery.py
import os
import json
import logging
from pathlib import Path
from typing import Dict, Any, List

log = logging.getLogger("daemon.discovery")

class PluginDiscovery:
    def __init__(self, plugins_root: str):
        self.root = Path(plugins_root)
        self.registry: Dict[str, Dict[str, Any]] = {}

    def scan(self) -> Dict[str, Dict[str, Any]]:
        """Scan public plugins directory for valid manifests."""
        log.info(f"Scanning plugins in {self.root}")
        new_registry = {}
        
        if not self.root.exists():
            log.warning(f"Plugins root {self.root} does not exist.")
            return {}

        for item in self.root.iterdir():
            if item.is_dir() and not item.name.startswith("_"):
                manifest_path = item / "plugin.json"
                if manifest_path.exists():
                    try:
                        with open(manifest_path, "r") as f:
                            manifest = json.load(f)
                            
                        # Basic validation
                        if manifest.get("$schema") == "magic-plugin-v1":
                            name = manifest.get("name", item.name)
                            manifest["path"] = str(item.absolute())
                            new_registry[name] = manifest
                            log.info(f"Discovered plugin: {name} (v{manifest.get('version', 'unknown')})")
                    except Exception as e:
                        log.error(f"Error loading plugin at {item}: {e}")
        
        self.registry = new_registry
        return self.registry

    def get_plugin(self, name: str) -> Dict[str, Any]:
        return self.registry.get(name)

    def list_plugins(self) -> List[str]:
        return list(self.registry.keys())
