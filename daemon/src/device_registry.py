"""
Device Registry Manager
Maintains authoritative list of fleet devices, hardware classes, and firmware versions.
Source of truth for OTA operations.
"""

import sqlite3
import json
import logging
from pathlib import Path
from typing import Dict, List, Optional
from dataclasses import dataclass, asdict
from datetime import datetime
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

logger = logging.getLogger(__name__)

@dataclass
class Device:
    """Device record."""
    device_id: str
    name: str
    hardware_class: str  # "V3" or "V4"
    ip_address: str
    current_version: str  # e.g., "0.0.16V4"
    status: str  # "online", "offline", "unknown"
    last_seen: str  # ISO timestamp
    created_at: str  # ISO timestamp
    custom_metadata: Optional[str] = None  # JSON string for future extensibility

class DeviceRegistryRequest(BaseModel):
    name: str
    hardware_class: str  # V3 or V4
    ip_address: str
    current_version: str

class DeviceRegistry:
    """
    SQLite-backed device registry with git versioning support.
    """

    def __init__(self, db_path: Path = None, export_path: Path = None):
        self.db_path = db_path or Path("daemon/data/device_registry.db")
        self.export_path = export_path or Path("daemon/data/device_registry.json")
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _init_db(self):
        """Initialize SQLite schema."""
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    hardware_class TEXT NOT NULL,
                    ip_address TEXT,
                    current_version TEXT,
                    status TEXT,
                    last_seen TEXT,
                    created_at TEXT,
                    custom_metadata TEXT
                )
            """)
            conn.execute("""
                CREATE TABLE IF NOT EXISTS registry_versions (
                    version TEXT PRIMARY KEY,
                    timestamp TEXT,
                    description TEXT,
                    device_count INTEGER
                )
            """)
            conn.commit()
        logger.info(f"Device registry initialized at {self.db_path}")

    def add_device(self, device_id: str, name: str, hardware_class: str,
                   ip_address: str, current_version: str) -> Device:
        """Add or update device in registry."""
        if hardware_class not in ["V3", "V4"]:
            raise ValueError(f"Invalid hardware_class: {hardware_class}. Must be V3 or V4")

        now = datetime.utcnow().isoformat() + "Z"
        device = Device(
            device_id=device_id,
            name=name,
            hardware_class=hardware_class,
            ip_address=ip_address,
            current_version=current_version,
            status="unknown",
            last_seen=now,
            created_at=now
        )

        with sqlite3.connect(self.db_path) as conn:
            conn.execute("""
                INSERT OR REPLACE INTO devices
                (device_id, name, hardware_class, ip_address, current_version, status, last_seen, created_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                device.device_id, device.name, device.hardware_class,
                device.ip_address, device.current_version, device.status,
                device.last_seen, device.created_at
            ))
            conn.commit()

        logger.info(f"Device registered: {device_id} ({hardware_class})")
        return device

    def get_device(self, device_id: str) -> Optional[Device]:
        """Get device by ID."""
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            row = conn.execute(
                "SELECT * FROM devices WHERE device_id = ?",
                (device_id,)
            ).fetchone()

        if row:
            return Device(**dict(row))
        return None

    def list_devices(self, hardware_class: Optional[str] = None) -> List[Device]:
        """List all devices, optionally filtered by hardware class."""
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            if hardware_class:
                rows = conn.execute(
                    "SELECT * FROM devices WHERE hardware_class = ? ORDER BY device_id",
                    (hardware_class,)
                ).fetchall()
            else:
                rows = conn.execute(
                    "SELECT * FROM devices ORDER BY device_id"
                ).fetchall()

        return [Device(**dict(row)) for row in rows]

    def update_version(self, device_id: str, version: str) -> Device:
        """Update device firmware version."""
        device = self.get_device(device_id)
        if not device:
            raise ValueError(f"Device not found: {device_id}")

        now = datetime.utcnow().isoformat() + "Z"
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                "UPDATE devices SET current_version = ?, last_seen = ?, status = ? WHERE device_id = ?",
                (version, now, "online", device_id)
            )
            conn.commit()

        logger.info(f"Device {device_id} updated to version {version}")
        return self.get_device(device_id)

    def update_status(self, device_id: str, status: str) -> Optional[Device]:
        """Update device status (online/offline/unknown)."""
        device = self.get_device(device_id)
        if not device:
            return None

        now = datetime.utcnow().isoformat() + "Z"
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                "UPDATE devices SET status = ?, last_seen = ? WHERE device_id = ?",
                (status, now, device_id)
            )
            conn.commit()

        return self.get_device(device_id)

    def export_to_json(self) -> Dict:
        """Export registry to JSON (for git versioning)."""
        devices = self.list_devices()
        registry = {
            "version": "1.0.0",
            "timestamp": datetime.utcnow().isoformat() + "Z",
            "device_count": len(devices),
            "devices": {d.device_id: asdict(d) for d in devices}
        }

        # Write to file
        with open(self.export_path, 'w') as f:
            json.dump(registry, f, indent=2)

        logger.info(f"Registry exported to {self.export_path}")
        return registry

    def import_from_json(self, json_path: Path) -> int:
        """Import devices from JSON file."""
        with open(json_path, 'r') as f:
            data = json.load(f)

        count = 0
        for device_id, device_info in data.get("devices", {}).items():
            self.add_device(
                device_id=device_id,
                name=device_info.get("name", device_id),
                hardware_class=device_info.get("hardware_class", "V4"),
                ip_address=device_info.get("ip_address", ""),
                current_version=device_info.get("current_version", "0.0.0V4")
            )
            count += 1

        logger.info(f"Imported {count} devices from {json_path}")
        return count

    def get_router(self) -> APIRouter:
        """Create FastAPI router for device registry endpoints."""
        router = APIRouter(prefix="/api/registry", tags=["registry"])

        @router.get("/devices")
        async def list_all_devices(hardware_class: Optional[str] = None):
            """List devices, optionally filtered by hardware class."""
            devices = self.list_devices(hardware_class)
            return {
                "count": len(devices),
                "devices": [asdict(d) for d in devices]
            }

        @router.get("/devices/{device_id}")
        async def get_single_device(device_id: str):
            """Get specific device."""
            device = self.get_device(device_id)
            if not device:
                raise HTTPException(status_code=404, detail="Device not found")
            return asdict(device)

        @router.post("/devices")
        async def register_device(req: DeviceRegistryRequest):
            """Register new device."""
            try:
                device = self.add_device(
                    device_id=f"{req.hardware_class.lower()}_{len(self.list_devices()) + 1:03d}",
                    name=req.name,
                    hardware_class=req.hardware_class,
                    ip_address=req.ip_address,
                    current_version=req.current_version
                )
                self.export_to_json()
                return asdict(device)
            except ValueError as e:
                raise HTTPException(status_code=400, detail=str(e))

        @router.post("/devices/{device_id}/version")
        async def update_device_version(device_id: str, version: str):
            """Update device firmware version."""
            try:
                device = self.update_version(device_id, version)
                self.export_to_json()
                return asdict(device)
            except ValueError as e:
                raise HTTPException(status_code=404, detail=str(e))

        @router.post("/devices/{device_id}/status")
        async def update_device_status(device_id: str, status: str):
            """Update device status."""
            device = self.update_status(device_id, status)
            if not device:
                raise HTTPException(status_code=404, detail="Device not found")
            self.export_to_json()
            return asdict(device)

        @router.post("/export")
        async def export_registry():
            """Export registry to JSON file (git-tracked)."""
            return self.export_to_json()

        return router
