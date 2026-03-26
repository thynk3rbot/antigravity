"""
ServiceManager — the octopus.

Manages lifecycle of all LoRaLink system services as subprocesses.
Each service runs standalone and can be started/stopped independently.
The daemon starts them, monitors them, and exposes status via API.

Services are defined in daemon/config.json (auto=true starts on daemon boot).
Each service captures stdout/stderr for log tailing.
"""

import asyncio
import json
import logging
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Optional

logger = logging.getLogger(__name__)

REPO_ROOT = Path(__file__).parent.parent.parent


@dataclass
class ServiceDef:
    name: str
    cmd: str                        # command relative to repo root
    port: Optional[int]
    auto: bool                      # start on daemon boot
    cwd: Optional[str] = None       # working dir relative to repo root (default: repo root)
    description: str = ""


@dataclass
class ServiceState:
    definition: ServiceDef
    proc: Optional[asyncio.subprocess.Process] = None
    pid: Optional[int] = None
    started_at: Optional[float] = None
    logs: deque = field(default_factory=lambda: deque(maxlen=200))
    restart_count: int = 0

    @property
    def running(self) -> bool:
        return self.proc is not None and self.proc.returncode is None

    @property
    def uptime_s(self) -> Optional[int]:
        if self.started_at and self.running:
            return int(time.time() - self.started_at)
        return None

    def to_dict(self) -> dict:
        return {
            "name": self.definition.name,
            "description": self.definition.description,
            "running": self.running,
            "pid": self.pid,
            "port": self.definition.port,
            "uptime_s": self.uptime_s,
            "restart_count": self.restart_count,
            "auto": self.definition.auto,
        }


# Default service catalogue — overridden by daemon/config.json if present
DEFAULT_SERVICES = [
    ServiceDef(
        name="webapp",
        cmd="python tools/webapp/server.py",
        port=8000,
        auto=True,
        description="Fleet Admin Webapp (port 8000)",
    ),
    ServiceDef(
        name="assistant",
        cmd="python tools/assistant/main.py",
        port=8300,
        auto=False,
        description="LoRaLink AI Assistant (port 8300)",
    ),
    ServiceDef(
        name="ollama_proxy",
        cmd="python tools/multi-agent-framework/hybrid_model_proxy.py",
        port=None,
        auto=False,
        description="Hybrid Model Proxy (Ollama + OpenRouter)",
    ),
]


class ServiceManager:
    def __init__(self):
        self._services: Dict[str, ServiceState] = {}
        self._load_services()

    def _load_services(self):
        config_path = REPO_ROOT / "daemon" / "config.json"
        service_defs = DEFAULT_SERVICES[:]

        if config_path.exists():
            try:
                cfg = json.loads(config_path.read_text())
                overrides = {s["name"]: s for s in cfg.get("services", [])}
                # Apply overrides to defaults, add any new ones
                for svc in service_defs:
                    if svc.name in overrides:
                        o = overrides.pop(svc.name)
                        svc.auto = o.get("auto", svc.auto)
                        svc.cmd = o.get("cmd", svc.cmd)
                        svc.port = o.get("port", svc.port)
                        svc.description = o.get("description", svc.description)
                for name, o in overrides.items():
                    service_defs.append(ServiceDef(
                        name=name,
                        cmd=o["cmd"],
                        port=o.get("port"),
                        auto=o.get("auto", False),
                        description=o.get("description", ""),
                    ))
            except Exception as e:
                logger.warning(f"[Services] Failed to load config.json: {e}")

        for svc in service_defs:
            self._services[svc.name] = ServiceState(definition=svc)

    async def start_auto_services(self):
        """Start all services marked auto=true. Called by daemon on boot."""
        for name, state in self._services.items():
            if state.definition.auto:
                logger.info(f"[Services] Auto-starting: {name}")
                await self.start(name)

    async def start(self, name: str) -> dict:
        state = self._services.get(name)
        if not state:
            return {"ok": False, "error": f"Unknown service: {name}"}
        if state.running:
            return {"ok": False, "error": f"{name} already running (pid {state.pid})"}

        cwd = REPO_ROOT
        if state.definition.cwd:
            cwd = REPO_ROOT / state.definition.cwd

        try:
            proc = await asyncio.create_subprocess_shell(
                state.definition.cmd,
                cwd=str(cwd),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            state.proc = proc
            state.pid = proc.pid
            state.started_at = time.time()
            state.restart_count += 1
            logger.info(f"[Services] Started {name} (pid {proc.pid})")
            asyncio.create_task(self._drain_logs(name, proc))
            return {"ok": True, "pid": proc.pid}
        except Exception as e:
            logger.error(f"[Services] Failed to start {name}: {e}")
            return {"ok": False, "error": str(e)}

    async def stop(self, name: str) -> dict:
        state = self._services.get(name)
        if not state:
            return {"ok": False, "error": f"Unknown service: {name}"}
        if not state.running:
            return {"ok": False, "error": f"{name} not running"}

        try:
            state.proc.terminate()
            try:
                await asyncio.wait_for(state.proc.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                state.proc.kill()
            state.proc = None
            state.pid = None
            state.started_at = None
            logger.info(f"[Services] Stopped {name}")
            return {"ok": True}
        except Exception as e:
            return {"ok": False, "error": str(e)}

    async def restart(self, name: str) -> dict:
        await self.stop(name)
        await asyncio.sleep(1)
        return await self.start(name)

    def status(self, name: str) -> Optional[dict]:
        state = self._services.get(name)
        return state.to_dict() if state else None

    def status_all(self) -> dict:
        return {name: state.to_dict() for name, state in self._services.items()}

    def logs(self, name: str, lines: int = 50) -> list:
        state = self._services.get(name)
        if not state:
            return []
        return list(state.logs)[-lines:]

    async def _drain_logs(self, name: str, proc: asyncio.subprocess.Process):
        """Continuously read stdout into the service's log ring buffer."""
        state = self._services.get(name)
        if not state:
            return
        try:
            async for line in proc.stdout:
                text = line.decode(errors="replace").rstrip()
                state.logs.append(text)
        except Exception:
            pass
        logger.info(f"[Services] {name} process exited (rc={proc.returncode})")

    async def shutdown_all(self):
        """Gracefully stop all running services. Called on daemon shutdown."""
        for name in list(self._services.keys()):
            if self._services[name].running:
                await self.stop(name)
