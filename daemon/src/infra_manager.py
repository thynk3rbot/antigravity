import asyncio
import socket
import logging
import subprocess
import os
from pathlib import Path

logger = logging.getLogger(__name__)

class InfraManager:
    """
    Magic Infrastructure Orchestrator (Docker-Aware).
    Manages the Mosquitto and PostgreSQL containers.
    """
    def __init__(self, compose_dir: Path):
        self.compose_dir = compose_dir
        self.ports = [1883, 5432]
        self._is_ready = False
        self.docker_path = Path("C:/Program Files/Docker/Docker/Docker Desktop.exe")

    def is_engine_ready(self) -> bool:
        """Probe if the Docker engine is running."""
        try:
            # Short timeout check
            subprocess.run(["docker", "info"], 
                           stdout=subprocess.DEVNULL, 
                           stderr=asyncio.subprocess.DEVNULL, 
                           timeout=2, 
                           check=True)
            return True
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
            return False

    def launch_docker_desktop(self):
        """Proactively start Docker Desktop on Windows."""
        if self.docker_path.exists():
            logger.info(f"[Magic] Launching Docker Desktop from {self.docker_path}...")
            os.startfile(str(self.docker_path))
            return True
        logger.error(f"[Magic] Docker Desktop not found at {self.docker_path}")
        return False

    async def ensure_up(self, timeout=30):
        """Bring Docker containers up. Detects engine status."""
        if not self.is_engine_ready():
            logger.warning("[Magic] Docker Engine is OFFLINE. Cannot start infrastructure.")
            return False

        logger.info("[Magic] Orchestrating Infrastructure (Docker)...")
        try:
            proc = await asyncio.create_subprocess_shell(
                "docker-compose up -d",
                cwd=str(self.compose_dir),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            stdout, _ = await proc.communicate()
            if proc.returncode != 0:
                logger.error(f"[Magic] Docker Compose failed: {stdout.decode()}")
                return False
                
            start_time = asyncio.get_event_loop().time()
            while asyncio.get_event_loop().time() - start_time < timeout:
                if all(self._probe_port(p) for p in self.ports):
                    logger.info("[Magic] Infrastructure is READY.")
                    self._is_ready = True
                    return True
                await asyncio.sleep(1)
            
            logger.warning("[Magic] Infrastructure timeout reaching ports.")
            return False
        except Exception as e:
            logger.error(f"[Magic] Infrastructure Error: {e}")
            return False

    def _probe_port(self, port: int) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.5)
            return s.connect_ex(('localhost', port)) == 0

    def status(self) -> dict:
        engine_up = self.is_engine_ready()
        ready_ports = {p: self._probe_port(p) for p in self.ports}
        return {
            "engine_ready": engine_up,
            "ready": engine_up and all(ready_ports.values()),
            "ports": ready_ports
        }

    async def restart(self):
        logger.info("[Magic] Restarting Infrastructure...")
        if not self.is_engine_ready():
            self.launch_docker_desktop()
            return

        proc = await asyncio.create_subprocess_shell(
            "docker-compose restart",
            cwd=str(self.compose_dir),
        )
        await proc.wait()
        await self.ensure_up()
