"""
Phase 50 Mesh API: REST endpoints for autonomous mesh sovereignty.

Endpoints:
  POST   /api/mesh/command              — Send command to device
  GET    /api/mesh/command/{cmd_id}     — Check command status
  GET    /api/mesh/topology             — Get mesh graph visualization
  POST   /api/mesh/command-queue        — Queue command for offline device
  GET    /api/mesh/node/{node_id}       — Get device status
  GET    /api/mesh/stats                — Get router statistics
"""

from fastapi import APIRouter, HTTPException, BackgroundTasks
from pydantic import BaseModel
from typing import Optional, List, Dict
import logging
import uuid

from mesh_router import MeshRouter, MeshCommand, CommandStatus, TransportType

logger = logging.getLogger(__name__)

# Global router instance (would be dependency-injected in production)
router_instance: Optional[MeshRouter] = None


class SendCommandRequest(BaseModel):
    """Request to send a command to a device."""
    target_node: str
    action: str  # "gpio_toggle", "gpio_set", "gpio_read", etc.
    pin: int
    duration_ms: Optional[int] = None


class SendCommandResponse(BaseModel):
    """Response from command send."""
    cmd_id: str
    status: str  # "sent", "queued"
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
    transport: str
    reachable: bool
    neighbors: List[str]
    battery_mv: Optional[int] = None
    uptime_ms: Optional[int] = None


class TopologyResponse(BaseModel):
    """Mesh topology for visualization."""
    node_count: int
    peers: List[PeerInfo]
    own_node_id: str


class DeviceStatusResponse(BaseModel):
    """Status of a single device."""
    node_id: str
    uptime_ms: Optional[int]
    battery_mv: Optional[int]
    relay_states: Optional[List[int]]
    last_seen: float
    pending_commands: int
    is_online: bool


# Create router
api = APIRouter(prefix="/api/mesh", tags=["mesh"])


def init_mesh_api(router: MeshRouter) -> APIRouter:
    """Initialize mesh API with a router instance."""
    global router_instance
    router_instance = router
    return api


@api.post("/command", response_model=SendCommandResponse)
async def send_command(request: SendCommandRequest) -> SendCommandResponse:
    """
    Send a command to a device via mesh.

    Routes intelligently:
    - Direct if device online
    - Multi-hop if device reachable through neighbors
    - Queued if device offline
    """
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    # Create command
    cmd = MeshCommand(
        cmd_id=str(uuid.uuid4()),
        from_node="daemon-api",
        to_node=request.target_node,
        action=request.action,
        pin=request.pin,
        duration_ms=request.duration_ms,
    )

    # Route it
    success, msg = router_instance.route_command(cmd)
    if not success:
        raise HTTPException(status_code=400, detail=msg)

    return SendCommandResponse(
        cmd_id=cmd.cmd_id,
        status=cmd.status.value,
        target_node=request.target_node,
        message=msg,
    )


@api.get("/command/{cmd_id}", response_model=CommandStatusResponse)
async def check_command_status(cmd_id: str) -> CommandStatusResponse:
    """Check status of a command by ID."""
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    cmd_data = router_instance.get_command_status(cmd_id)
    if not cmd_data:
        raise HTTPException(status_code=404, detail=f"Command {cmd_id} not found")

    return CommandStatusResponse(
        cmd_id=cmd_data["cmd_id"],
        status=cmd_data["status"],
        result=cmd_data.get("result"),
        error_message=cmd_data.get("error_message"),
        timestamp_ms=cmd_data["queued_at_ms"],
    )


@api.get("/topology", response_model=TopologyResponse)
async def get_mesh_topology() -> TopologyResponse:
    """Get complete mesh topology for visualization."""
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    topo = router_instance.get_topology()

    return TopologyResponse(
        node_count=topo["node_count"],
        peers=[
            PeerInfo(
                node_id=p["node_id"],
                mac_address=p["mac_address"],
                rssi_dbm=p["rssi_dbm"],
                transport=p["transport"].value if hasattr(p["transport"], "value") else p["transport"],
                reachable=p["reachable"],
                neighbors=p["neighbors"],
                battery_mv=p.get("battery_mv"),
                uptime_ms=p.get("uptime_ms"),
            )
            for p in topo["peers"]
        ],
        own_node_id=topo["own_node_id"],
    )


@api.post("/command-queue", response_model=SendCommandResponse)
async def queue_command_for_offline_device(
    request: SendCommandRequest,
) -> SendCommandResponse:
    """
    Queue a command for an offline device.

    Command will retry when device comes back online.
    """
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    # Create command (routing handles queueing internally)
    cmd = MeshCommand(
        cmd_id=str(uuid.uuid4()),
        from_node="daemon-api",
        to_node=request.target_node,
        action=request.action,
        pin=request.pin,
        duration_ms=request.duration_ms,
    )

    success, msg = router_instance.route_command(cmd)
    if not success:
        raise HTTPException(status_code=400, detail=msg)

    return SendCommandResponse(
        cmd_id=cmd.cmd_id,
        status=cmd.status.value,
        target_node=request.target_node,
        message=msg,
    )


@api.get("/node/{node_id}", response_model=DeviceStatusResponse)
async def get_device_status(node_id: str) -> DeviceStatusResponse:
    """Get detailed status of a device."""
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    peer = router_instance.get_peer(node_id)
    if not peer:
        raise HTTPException(status_code=404, detail=f"Node {node_id} not found")

    # Count pending commands for this device
    pending = sum(
        1 for cmd in router_instance.pending_commands.values()
        if cmd.to_node == node_id
    )

    return DeviceStatusResponse(
        node_id=node_id,
        uptime_ms=peer.uptime_ms,
        battery_mv=peer.battery_mv,
        relay_states=None,  # TODO: Get from device telemetry
        last_seen=peer.last_seen,
        pending_commands=pending,
        is_online=peer.reachable,
    )


@api.get("/stats")
async def get_mesh_stats() -> Dict:
    """Get mesh router statistics."""
    if not router_instance:
        raise HTTPException(status_code=500, detail="Router not initialized")

    return router_instance.get_stats()


@api.get("/health")
async def health_check() -> Dict:
    """Health check endpoint."""
    if not router_instance:
        return {"status": "unhealthy", "reason": "Router not initialized"}

    return {
        "status": "healthy",
        "peers": len(router_instance.peer_registry),
        "queued": len(router_instance.command_queue),
    }
