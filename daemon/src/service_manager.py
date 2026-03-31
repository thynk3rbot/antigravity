"""
ServiceManager — the octopus.
Manages lifecycle of all Magic system services as subprocesses.
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
  auto_restart: bool = True       # restart if it crashes
  cwd: Optional[str] = None       # working dir relative to repo root
  description: str = ""
  meshtastic_host: Optional[str] = None

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
      "auto_restart": self.definition.auto_restart,
    }

# Default service catalogue
DEFAULT_SERVICES = [
  ServiceDef(
    name="webapp",
    cmd="python tools/webapp/server.py",
    port=8000,
    auto=True,
    auto_restart=True,
    description="Magic Dashboard — Fleet monitor & device control (port 8000)",
  ),
  ServiceDef(
    name="magic_lvc",
    cmd="python daemon/src/lvc_service.py",
    port=None,
    auto=True,
    auto_restart=True,
    description="Magic Cache LVC Service",
  ),
  ServiceDef(
    name="magic_transmitter",
    cmd="python daemon/src/transmitter.py",
    port=None,
    auto=False,
    auto_restart=False,
    description="Magic Cache Data Spoofer",
  ),
]

class ServiceManager:
  def __init__(self):
    self._services: Dict[str, ServiceState] = {}
    self._monitor_task: Optional[asyncio.Task] = None
    self._load_services()

  def _load_services(self):
    config_path = REPO_ROOT / "daemon" / "config.json"
    service_defs = DEFAULT_SERVICES[:]

    if config_path.exists():
      try:
        cfg = json.loads(config_path.read_text())
        overrides = {s["name"]: s for s in cfg.get("services", [])}
        for svc in service_defs:
          if svc.name in overrides:
            o = overrides.pop(svc.name)
            svc.auto = o.get("auto", svc.auto)
            svc.cmd = o.get("cmd", svc.cmd)
            svc.port = o.get("port", svc.port)
            svc.auto_restart = o.get("auto_restart", svc.auto_restart)
        for name, o in overrides.items():
          service_defs.append(ServiceDef(
            name=name,
            cmd=o["cmd"],
            port=o.get("port"),
            auto=o.get("auto", False),
            auto_restart=o.get("auto_restart", True),
            description=o.get("description", ""),
            meshtastic_host=o.get("meshtastic_host"),
          ))
      except Exception as e:
        logger.warning(f"[Services] Failed to load config.json: {e}")

    for svc in service_defs:
      self._services[svc.name] = ServiceState(definition=svc)

  async def start_auto_services(self):
    """Start all services marked auto=true and begin monitoring."""
    for name, state in self._services.items():
      if state.definition.auto:
        await self.start(name)
    
    if not self._monitor_task:
      self._monitor_task = asyncio.create_task(self._monitoring_loop())

  async def _monitoring_loop(self):
    """Proactively restart crashed services marked with auto_restart."""
    while True:
      await asyncio.sleep(5)
      for name, state in self._services.items():
        if state.definition.auto_restart and not state.running and state.definition.auto:
          logger.warning(f"[Services] {name} detected as DOWN. Restarting...")
          await self.start(name)

  async def start(self, name: str) -> dict:
    state = self._services.get(name)
    if not state:
      return {"ok": False, "error": f"Unknown service: {name}"}
    if state.running:
      return {"ok": True, "pid": state.pid}

    cwd = REPO_ROOT
    if state.definition.cwd:
      cwd = REPO_ROOT / state.definition.cwd

    full_cmd = state.definition.cmd
    if state.definition.name == "meshtastic_bridge" and state.definition.meshtastic_host:
      full_cmd += f" --host {state.definition.meshtastic_host}"

    try:
      proc = await asyncio.create_subprocess_shell(
        full_cmd,
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
    if not state or not state.running:
      return {"ok": False}
    try:
      state.proc.terminate()
      await asyncio.wait_for(state.proc.wait(), timeout=5.0)
      state.proc = None
      state.pid = None
      return {"ok": True}
    except Exception:
      if state.proc:
        state.proc.kill()
      state.proc = None
      return {"ok": True}

  def status_all(self) -> dict:
    return {name: state.to_dict() for name, state in self._services.items()}

  async def _drain_logs(self, name: str, proc: asyncio.subprocess.Process):
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
    if self._monitor_task:
      self._monitor_task.cancel()
    for name in list(self._services.keys()):
      await self.stop(name)
