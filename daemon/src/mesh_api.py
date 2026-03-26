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
