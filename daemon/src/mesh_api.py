"""
Pure Mesh Gateway API: REST endpoints for webapp integration.

Model: Devices mesh with each other via ControlPacket. Daemon monitors topology
and acts as a gateway for webapp to inject commands into the mesh.

Endpoints:
  POST   /api/mesh/command              — Publish command for devices to relay
  GET    /api/mesh/command/{cmd_id}     — Check command status
  GET    /api/mesh/topology             — Get mesh topology
  GET    /api/mesh/node/{node_id}       — Get device status
  GET    /api/mesh/stats                — Get topology statistics
"""

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from typing import Optional, List, Dict
import logging
import json

try:
    from .mesh_router import MeshTopology, MeshCommand, CommandStatus
except ImportError:
    from mesh_router import MeshTopology, MeshCommand, CommandStatus

logger = logging.getLogger(__name__)

# Global topology instance
topology_instance: Optional[MeshTopology] = None
mqtt_publisher = None  # Will be set by main.py


class SendCommandRequest(BaseModel):
    """Request to inject a command into the mesh."""
    target_node: str
    action: str  # "gpio_toggle", "gpio_set", "gpio_read", etc.
    pin: int
    duration_ms: Optional[int] = None


class SendCommandResponse(BaseModel):
    """Response from command publication."""
    cmd_id: str
    status: str  # "published"
    target_node: str
    message: str


class CommandStatusResponse(BaseModel):
    """Response with command status."""
    cmd_id: str
    status: str
    result: Optional[Dict] = None
    error_message: Optional[str] = None
    timestamp_ms: float


class PeerInfo(BaseModel):
    """Information about a peer device."""
    node_id: str
    mac_address: str
    rssi_dbm: int
    reachable: bool
    neighbors: List[str]
    battery_mv: Optional[int] = None
    uptime_ms: Optional[int] = None


class TopologyResponse(BaseModel):
    """Mesh topology for visualization."""
    node_count: int
    online_count: int
    peers: List[PeerInfo]
    own_node_id: str


class DeviceStatusResponse(BaseModel):
    """Status of a single device."""
    node_id: str
    uptime_ms: Optional[int]
    battery_mv: Optional[int]
    last_seen: float
    is_online: bool
    neighbors: List[str]


# Create router
api = APIRouter(prefix="/api/mesh", tags=["mesh"])


def init_mesh_api(topology: MeshTopology, mqtt_pub=None) -> APIRouter:
    """Initialize mesh API with topology instance and MQTT publisher."""
    global topology_instance, mqtt_publisher
    topology_instance = topology
    mqtt_publisher = mqtt_pub
    return api


@api.post("/command", response_model=SendCommandResponse)
async def send_command(request: SendCommandRequest) -> SendCommandResponse:
    """
    Inject a command into the mesh.

    Command is published to MQTT topic: device/{target_node}/mesh/command
    Devices will relay it to the target via ControlPacket routing.
    """
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    # Validate target exists
    if request.target_node not in topology_instance.peer_registry:
        raise HTTPException(
            status_code=404,
            detail=f"Target node {request.target_node} not in mesh"
        )

    # Create command
    cmd = topology_instance.create_command(
        target_node=request.target_node,
        action=request.action,
        pin=request.pin,
        duration_ms=request.duration_ms,
    )

    # Publish to MQTT
    if mqtt_publisher:
        mqtt_payload = {
            "cmd_id": cmd.cmd_id,
            "action": cmd.action,
            "pin": cmd.pin,
            "duration_ms": cmd.duration_ms,
        }
        success = await mqtt_publisher.publish_command(request.target_node, mqtt_payload)
        if not success:
            raise HTTPException(status_code=500, detail="Failed to publish to MQTT")

    # Track in topology
    topology_instance.publish_command(cmd)

    return SendCommandResponse(
        cmd_id=cmd.cmd_id,
        status=cmd.status.value,
        target_node=request.target_node,
        message=f"Command published to mesh for {request.target_node}",
    )


@api.get("/command/{cmd_id}", response_model=CommandStatusResponse)
async def check_command_status(cmd_id: str) -> CommandStatusResponse:
    """Check status of a command by ID."""
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    cmd_data = topology_instance.get_command_status(cmd_id)
    if not cmd_data:
        raise HTTPException(status_code=404, detail=f"Command {cmd_id} not found")

    return CommandStatusResponse(
        cmd_id=cmd_data["cmd_id"],
        status=cmd_data["status"],
        result=cmd_data.get("result"),
        error_message=cmd_data.get("error_message"),
        timestamp_ms=cmd_data["published_at_ms"],
    )


@api.get("/topology", response_model=TopologyResponse)
async def get_mesh_topology() -> TopologyResponse:
    """Get complete mesh topology for visualization."""
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    topo = topology_instance.get_topology()

    return TopologyResponse(
        node_count=topo["node_count"],
        online_count=topo["online_count"],
        peers=[
            PeerInfo(
                node_id=p["node_id"],
                mac_address=p["mac_address"],
                rssi_dbm=p["rssi_dbm"],
                reachable=p["reachable"],
                neighbors=p["neighbors"],
                battery_mv=p.get("battery_mv"),
                uptime_ms=p.get("uptime_ms"),
            )
            for p in topo["peers"]
        ],
        own_node_id=topo["own_node_id"],
    )


@api.get("/node/{node_id}", response_model=DeviceStatusResponse)
async def get_device_status(node_id: str) -> DeviceStatusResponse:
    """Get detailed status of a device."""
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    peer = topology_instance.get_peer(node_id)
    if not peer:
        raise HTTPException(status_code=404, detail=f"Node {node_id} not found")

    return DeviceStatusResponse(
        node_id=node_id,
        uptime_ms=peer.uptime_ms,
        battery_mv=peer.battery_mv,
        last_seen=peer.last_seen,
        is_online=peer.reachable,
        neighbors=peer.neighbors,
    )


@api.get("/stats")
async def get_mesh_stats() -> Dict:
    """Get mesh topology statistics."""
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    return topology_instance.get_stats()


@api.get("/health")
async def health_check() -> Dict:
    """Health check endpoint."""
    if not topology_instance:
        return {"status": "unhealthy", "reason": "Topology not initialized"}

    stats = topology_instance.get_stats()
    return {
        "status": "healthy",
        "peers_total": stats["total_peers"],
        "peers_online": stats["online_peers"],
        "active_commands": stats["active_commands"],
    }


# ── OTA Firmware Flash ────────────────────────────────────────────────────────
# Daemon is the actor: it knows device IPs and runs pio espota against them.
# Webapp calls daemon; daemon does the flash.

import asyncio
import uuid
import pathlib

_ota_jobs: Dict = {}  # job_id -> {status, ip, env, version, progress, error}

_FW_DIR = pathlib.Path(__file__).parent.parent.parent / "firmware" / "v2"
_PIO = pathlib.Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe"
if not _PIO.exists():
    _PIO = pathlib.Path("pio")


class OTAFlashRequest(BaseModel):
    env: str    # heltec_v3 or heltec_v4
    ip: str     # device IP (from topology)
    node_id: Optional[str] = None  # if provided, used for logging only


@api.post("/ota/flash")
async def ota_flash(request: OTAFlashRequest) -> Dict:
    """
    Flash firmware OTA to a device. Daemon runs pio espota against device IP.
    Returns job_id — poll /api/mesh/ota/status/{job_id} for progress.
    """
    job_id = str(uuid.uuid4())[:8]
    _ota_jobs[job_id] = {"status": "running", "ip": request.ip, "env": request.env, "progress": "starting..."}

    async def _run(job_id: str, env: str, ip: str):
        cmd = f'"{_PIO}" run --target upload --environment {env} --upload-port {ip}'
        logger.info(f"[OTA] {job_id}: flashing {ip} ({env})")
        try:
            proc = await asyncio.create_subprocess_shell(
                cmd, cwd=str(_FW_DIR),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            output = []
            async for line in proc.stdout:
                text = line.decode(errors="replace").strip()
                output.append(text)
                _ota_jobs[job_id]["progress"] = text[-120:]
            await proc.wait()
            if proc.returncode == 0:
                ver = next((l.split("->")[-1].strip().split('"')[0]
                            for l in output if "[VERSION]" in l and "->" in l), None)
                _ota_jobs[job_id].update({"status": "done", "version": ver})
                logger.info(f"[OTA] {job_id}: done — {ver or 'version unknown'}")
            else:
                err = next((l for l in reversed(output) if l), "unknown error")
                _ota_jobs[job_id].update({"status": "error", "error": err[-200:]})
                logger.error(f"[OTA] {job_id}: FAILED — {err[:80]}")
        except Exception as e:
            _ota_jobs[job_id].update({"status": "error", "error": str(e)})

    asyncio.create_task(_run(job_id, request.env, request.ip))
    return {"ok": True, "job_id": job_id}


@api.get("/ota/status/{job_id}")
async def ota_status(job_id: str) -> Dict:
    """Poll OTA flash job status."""
    job = _ota_jobs.get(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")
    return job


@api.get("/ota/fleet")
async def ota_fleet() -> Dict:
    """Return all online devices with their IPs and versions — for webapp OTA panel."""
    if not topology_instance:
        return {"devices": []}
    topo = topology_instance.get_topology()
    devices = []
    for p in topo.get("peers", []):
        if p.get("reachable") and p.get("ip"):
            devices.append({
                "node_id": p["node_id"],
                "ip": p["ip"],
                "ver": p.get("version", "?"),
                "hw": p.get("hw", "?"),
            })
    return {"devices": devices}
