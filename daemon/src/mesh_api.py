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

import asyncio
import time
from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
from typing import Optional, List, Dict
import logging
import json

try:
    from .mesh_router import MeshTopology, MeshCommand, CommandStatus
    from .http_gateway import HTTPGateway
    from .peer_ring import PeerRing
except ImportError:
    from mesh_router import MeshTopology, MeshCommand, CommandStatus
    from http_gateway import HTTPGateway
    from peer_ring import PeerRing

logger = logging.getLogger(__name__)

# Global instances
topology_instance: Optional[MeshTopology] = None
mqtt_publisher = None  # Will be set by main.py
http_gateway: Optional[HTTPGateway] = None  # Will be set by main.py
peer_ring: Optional[PeerRing] = None  # Will be set by main.py


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


def init_mesh_api(topology: MeshTopology, mqtt_pub=None, device_registry=None) -> APIRouter:
    """Initialize mesh API with topology instance, MQTT publisher, and HTTP gateway."""
    global topology_instance, mqtt_publisher, http_gateway, peer_ring
    topology_instance = topology
    mqtt_publisher = mqtt_pub

    # Initialize HTTP gateway for global device routing
    if device_registry:
        http_gateway = HTTPGateway(device_registry)
        # Peer ring for consistent hashing (start with empty, will be populated)
        peer_ring = PeerRing(peers=[])

    return api


@api.post("/command", response_model=SendCommandResponse)
async def send_command(request: SendCommandRequest) -> SendCommandResponse:
    """
    Inject a command into the mesh with intelligent routing.

    Routes via:
    1. HTTP (if device has IP and is reachable)
    2. MQTT (if device is in local mesh)
    3. Consistent hash ring (for future peer-to-peer at scale)

    Enables global device control without firmware changes.
    """
    if not topology_instance:
        raise HTTPException(status_code=500, detail="Topology not initialized")

    # Create command (tracks in topology)
    cmd = topology_instance.create_command(
        target_node=request.target_node,
        action=request.action,
        pin=request.pin,
        duration_ms=request.duration_ms,
    )

    # Prepare command payload
    cmd_payload = {
        "cmd_id": cmd.cmd_id,
        "action": cmd.action,
        "pin": cmd.pin,
        "duration_ms": cmd.duration_ms,
        "timestamp": time.time(),
    }

    # Routing decision logic
    transport_used = "unknown"
    device_in_local_mesh = request.target_node in topology_instance.peer_registry

    # Try HTTP first (best for global devices)
    if http_gateway and not device_in_local_mesh:
        logger.info(
            f"[Mesh] Routing {request.target_node} via HTTP "
            f"(not in local mesh, has HTTP gateway)"
        )
        http_result = await http_gateway.send_command(
            target_node=request.target_node,
            cmd=cmd_payload,
            fallback_mqtt=mqtt_publisher
        )

        if http_result.get("success"):
            transport_used = http_result.get("transport", "http")
            topology_instance.publish_command(cmd)
            return SendCommandResponse(
                cmd_id=cmd.cmd_id,
                status="published",
                target_node=request.target_node,
                message=f"Command routed via {transport_used} to {request.target_node}",
            )
        else:
            logger.warning(
                f"[Mesh] HTTP routing failed for {request.target_node}: "
                f"{http_result.get('error')}"
            )

    # Fallback to MQTT (local mesh relay)
    if mqtt_publisher:
        logger.info(
            f"[Mesh] Routing {request.target_node} via MQTT "
            f"(local mesh or HTTP failed)"
        )
        success = await mqtt_publisher.publish_command(request.target_node, cmd_payload)

        if success:
            transport_used = "mqtt"
            topology_instance.publish_command(cmd)
            return SendCommandResponse(
                cmd_id=cmd.cmd_id,
                status="published",
                target_node=request.target_node,
                message=f"Command published via {transport_used} for {request.target_node}",
            )
        else:
            raise HTTPException(status_code=500, detail="Failed to publish via MQTT")

    # No transport available
    raise HTTPException(
        status_code=503,
        detail=f"No route to {request.target_node} "
        f"(not in local mesh, HTTP unavailable, MQTT unavailable)"
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


# ── Peer Ring: Consistent Hash for 1000s of Devices ────────────────────────────

@api.get("/ring/export")
async def export_peer_ring() -> Dict:
    """
    Export peer ring for devices to sync (gossip protocol support).

    Devices download this and apply same hash function for routing.
    Enables: Device-to-device routing without central registry.
    """
    if not peer_ring:
        return {"peers": [], "virtual_nodes": 3, "message": "Peer ring not initialized"}

    return peer_ring.export()


@api.post("/ring/update")
async def update_peer_ring(peers: List[str]) -> Dict:
    """Update peer ring with new list of devices."""
    global peer_ring

    if not peer_ring:
        raise HTTPException(status_code=500, detail="Peer ring not initialized")

    peer_ring.peers = peers
    peer_ring.rebuild()

    logger.info(f"[PeerRing] Updated with {len(peers)} peers")
    return {
        "status": "updated",
        "peers_count": len(peers),
        "ring_size": len(peer_ring.ring)
    }


@api.get("/ring/route/{target_device_id}")
async def get_route(target_device_id: str) -> Dict:
    """
    Query: Which peer should handle this device?

    Example: /ring/route/DEV042
    Response: {"target": "DEV042", "responsible_peer": "DEV001", "replicas": ["DEV001", "DEV003"]}

    Useful for: Debugging routing, understanding consistent hash behavior.
    """
    if not peer_ring:
        raise HTTPException(status_code=500, detail="Peer ring not initialized")

    primary = peer_ring.get_peer(target_device_id)
    replicas = peer_ring.get_peers(target_device_id, replicas=3)

    return {
        "target": target_device_id,
        "responsible_peer": primary,
        "replicas": replicas,
        "ring_size": len(peer_ring.peers),
        "virtual_nodes": peer_ring.virtual_nodes
    }


# ── OTA Firmware Flash ────────────────────────────────────────────────────────
# Daemon is the actor: it knows device IPs and runs pio espota against them.
# Webapp calls daemon; daemon does the flash.

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


# ── Server-Sent Events: live topology push ────────────────────────────────────

@api.get("/events")
async def topology_events(request: Request):
    """
    SSE stream of mesh topology updates.
    Sends current topology immediately, then pushes on every change (2s poll interval).
    Browser: new EventSource('http://localhost:8001/api/mesh/events')
    """
    async def _stream():
        last_hash = None
        while True:
            if await request.is_disconnected():
                break
            if topology_instance:
                topo = topology_instance.get_topology()
                topo_hash = hash(json.dumps(topo, sort_keys=True, default=str))
                if topo_hash != last_hash:
                    last_hash = topo_hash
                    yield f"data: {json.dumps(topo)}\n\n"
            else:
                yield f"data: {json.dumps({'peers': [], 'node_count': 0, 'online_count': 0})}\n\n"
            await asyncio.sleep(2)

    return StreamingResponse(
        _stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
        },
    )
