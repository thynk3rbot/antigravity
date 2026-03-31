"""
OTA Manager: Handles remote device flashing by invoking platformio over espota.
Includes device registry guards to prevent cross-flashing (V3 fw to V4 device, etc).
"""

import asyncio
import uuid
import logging
from typing import Dict, Any, Optional
from pathlib import Path
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

logger = logging.getLogger(__name__)

class FlashRequest(BaseModel):
    device_id: Optional[str] = None  # Prefer device_id (from registry)
    env: Optional[str] = None  # Fallback: env directly (legacy)
    ip: str = ""

class BatchFlashRequest(BaseModel):
    hardware_class: str  # "V3" or "V4"

class FlashGuards:
    """Guard rules for safe OTA operations."""
    ENV_TO_HARDWARE = {
        "heltec_v4": "V4",
        "heltec_v3": "V3",
        "heltec_v3_node": "V3",
    }

class OtaManager:
    def __init__(self, topology, repo_root: Path, device_registry=None):
        self.topology = topology
        self.repo_root = repo_root
        self.firmware_dir = repo_root / "firmware" / "v2"
        self.device_registry = device_registry
        self.active_jobs: Dict[str, Dict[str, Any]] = {}
        self.guards = FlashGuards()

    def get_router(self) -> APIRouter:
        router = APIRouter(prefix="/api/ota", tags=["ota"])

        @router.get("/fleet")
        async def get_fleet():
            # Extract peers that have IPs
            peers = self.topology.list_peers()
            flashable = [
                {
                    "node_id": p.node_id, 
                    "name": p.node_id, 
                    "ip": p.ip_address, 
                    "ver": getattr(p, "fw_ver", "unknown")
                }
                for p in peers if p.reachable and getattr(p, 'ip_address', None)
            ]
            return {"devices": flashable}

        @router.post("/flash")
        async def start_flash(req: FlashRequest):
            """
            Flash device with guards.
            Accepts either device_id (preferred, uses registry) or env directly (legacy).
            """
            # Resolve device_id and env with guards
            env = req.env
            device_id = req.device_id
            ip = req.ip

            # If device_id provided, look up from registry
            if device_id and self.device_registry:
                device = self.device_registry.get_device(device_id)
                if not device:
                    raise HTTPException(status_code=404, detail=f"Device not found in registry: {device_id}")

                # Infer env from hardware class
                if device.hardware_class == "V4":
                    env = "heltec_v4"
                elif device.hardware_class == "V3":
                    env = "heltec_v3"
                else:
                    raise HTTPException(status_code=400, detail=f"Unknown hardware class: {device.hardware_class}")

                ip = device.ip_address

                logger.info(f"Flash request for {device_id} ({device.hardware_class}) → {env} @ {ip}")
            elif not env:
                raise HTTPException(status_code=400, detail="Must provide either device_id or env")

            job_id = str(uuid.uuid4())
            self.active_jobs[job_id] = {
                "id": job_id,
                "status": "running",
                "device_id": device_id,
                "env": env,
                "ip": ip,
                "log": []
            }
            # Start background task
            asyncio.create_task(self._run_pio_flash(job_id, env, ip, device_id))
            return {"ok": True, "job_id": job_id, "device_id": device_id, "env": env}

        @router.post("/flash/by-class")
        async def flash_by_class(req: BatchFlashRequest):
            """Flash all online devices of a given hardware class in parallel."""
            if req.hardware_class not in ("V3", "V4"):
                raise HTTPException(status_code=400, detail="hardware_class must be 'V3' or 'V4'")
            if not self.device_registry:
                raise HTTPException(status_code=500, detail="Device registry not available")

            devices = self.device_registry.list_devices(hardware_class=req.hardware_class)
            online = [d for d in devices if d.status == "online" and d.ip_address]
            if not online:
                raise HTTPException(
                    status_code=404,
                    detail=f"No online {req.hardware_class} devices found in registry"
                )

            batch_id = str(uuid.uuid4())
            jobs = []
            for device in online:
                env = "heltec_v4" if device.hardware_class == "V4" else "heltec_v3"
                job_id = str(uuid.uuid4())
                self.active_jobs[job_id] = {
                    "id": job_id,
                    "status": "running",
                    "device_id": device.device_id,
                    "env": env,
                    "ip": device.ip_address,
                    "log": [],
                    "batch_id": batch_id,
                }
                asyncio.create_task(
                    self._run_pio_flash(job_id, env, device.ip_address, device.device_id)
                )
                jobs.append({"device_id": device.device_id, "job_id": job_id})
                logger.info(
                    f"[OTA:batch:{batch_id}] Spawned {job_id} for "
                    f"{device.device_id} ({env}) @ {device.ip_address}"
                )

            return {"batch_id": batch_id, "jobs": jobs}

        @router.get("/status/{job_id}")
        async def get_status(job_id: str):
            if job_id not in self.active_jobs:
                return {"status": "error", "error": "Job not found"}
            job = self.active_jobs[job_id]
            return {
                "status": job["status"], 
                "error": job.get("error"), 
                "log": job["log"]
            }

        return router

    async def _run_pio_flash(self, job_id: str, env: str, ip: str, device_id: Optional[str] = None):
        job = self.active_jobs[job_id]

        # Validate env against whitelist (security: prevent injection)
        valid_envs = ["heltec_v3", "heltec_v4", "heltec_v3_node"]
        if env not in valid_envs:
            job["status"] = "error"
            job["error"] = f"Invalid environment: {env}"
            job["log"].append(f"ERROR: {job['error']}")
            logger.error(f"[OTA:{job_id}] Invalid env: {env}")
            return

        # Build platformio command (safe: list of strings, no shell injection)
        cmd = ["pio", "run", "-e", env, "-t", "upload"]

        if ip:
            # If IP is provided, use espota
            cmd.extend(["--upload-port", ip])
            logger.info(f"[OTA:{job_id}] Executing OTA: {' '.join(cmd)}")
            job["log"].append(f"Starting PIO OTA flash for {env} to {ip}...")
        else:
            # Otherwise, allow PlatformIO to auto-detect the local USB serial port
            logger.info(f"[OTA:{job_id}] Executing Serial Auto-Detect: {' '.join(cmd)}")
            job["log"].append(f"Starting PIO USB/Serial flash for {env} (auto-detect port)...")

        try:
            process = await asyncio.create_subprocess_exec(
                *cmd,
                cwd=str(self.firmware_dir),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT
            )

            while True:
                line = await process.stdout.readline()
                if not line:
                    break
                decoded = line.decode('utf-8', errors='replace').strip()
                if decoded:
                    job["log"].append(decoded)
                    # Limit log buffer
                    if len(job["log"]) > 1000:
                        job["log"] = job["log"][-1000:]
                    logger.debug(f"[OTA:{job_id}] {decoded}")

            await process.wait()

            if process.returncode == 0:
                job["status"] = "done"
                job["log"].append("")
                job["log"].append("SUCCESS: Firmware flashed successfully!")

                # Update registry if device_id provided
                if device_id and self.device_registry:
                    try:
                        # Mark device as online - version will be confirmed by STATUS query
                        self.device_registry.update_status(device_id, "online")
                        logger.info(f"[OTA:{job_id}] Registry updated for {device_id}")
                    except Exception as e:
                        logger.error(f"[OTA:{job_id}] Failed to update registry: {e}")
                        job["log"].append(f"Warning: Registry update failed: {e}")

                logger.info(f"[OTA:{job_id}] Flashed successfully to {ip}.")
            else:
                job["status"] = "error"
                job["error"] = f"PIO exited with code {process.returncode}"
                job["log"].append("")
                job["log"].append("ERROR: Firmware flash failed.")
                logger.error(f"[OTA:{job_id}] Flash failed! Exited with {process.returncode}.")

        except Exception as e:
            job["status"] = "error"
            job["error"] = f"Failed to start PlatformIO process: {e}"
            job["log"].append(f"ERROR: {e}")
            logger.error(f"[OTA:{job_id}] Exception starting PIO: {e}")
