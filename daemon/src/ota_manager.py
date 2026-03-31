"""
OTA Manager: Handles remote device flashing by invoking platformio over espota.
"""

import asyncio
import uuid
import logging
from typing import Dict, Any
from pathlib import Path
from fastapi import APIRouter
from pydantic import BaseModel

logger = logging.getLogger(__name__)

class FlashRequest(BaseModel):
    env: str
    ip: str = ""

class OtaManager:
    def __init__(self, topology, repo_root: Path):
        self.topology = topology
        self.repo_root = repo_root
        self.firmware_dir = repo_root / "firmware" / "v2"
        self.active_jobs: Dict[str, Dict[str, Any]] = {}

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
            job_id = str(uuid.uuid4())
            self.active_jobs[job_id] = {
                "id": job_id,
                "status": "running",
                "env": req.env,
                "ip": req.ip,
                "log": []
            }
            # Start background task
            asyncio.create_task(self._run_pio_flash(job_id, req.env, req.ip))
            return {"ok": True, "job_id": job_id}

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

    async def _run_pio_flash(self, job_id: str, env: str, ip: str):
        job = self.active_jobs[job_id]
        
        # Build platformio command.
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
