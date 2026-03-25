from fastapi import FastAPI, HTTPException
from typing import List, Optional
from tools.daemon.models import Node, Message, MessageStatus, ProvisionRequest, ProvisionResponse, CarrierProfile
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager
import logging
import json
from pathlib import Path

logger = logging.getLogger(__name__)


def create_api_app(message_queue: MessageQueue, transport_manager: TransportManager) -> FastAPI:
    """Create FastAPI application with daemon endpoints.

    Args:
        message_queue: SQLite-backed message persistence
        transport_manager: Multi-transport routing manager

    Returns:
        Configured FastAPI application instance
    """
    app = FastAPI(
        title="LoRaLink Daemon API",
        description="PC Daemon transport hub for LoRaLink ESP32 swarm",
        version="0.1.0"
    )

    # In-memory node registry (persisted state lives in transport/NVS)
    nodes: List[Node] = []

    # ─────────────────────────────────────────────────────────
    # Node Management
    # ─────────────────────────────────────────────────────────

    @app.post("/api/nodes")
    async def add_node(node_data: dict):
        """Register a new node with the daemon.

        Payload: {id, name, type, address}
        """
        # Reject duplicate node IDs
        if any(n.id == node_data.get("id") for n in nodes):
            raise HTTPException(status_code=409, detail=f"Node {node_data['id']} already registered")

        node = Node(
            id=node_data["id"],
            name=node_data["name"],
            type=node_data["type"],
            address=node_data["address"]
        )
        nodes.append(node)
        logger.info(f"Registered node: {node.name} ({node.id})")
        return node.to_dict()

    @app.get("/api/nodes")
    async def list_nodes():
        """List all known nodes."""
        return [n.to_dict() for n in nodes]

    @app.get("/api/nodes/{node_id}")
    async def get_node(node_id: str):
        """Get specific node by ID."""
        node = next((n for n in nodes if n.id == node_id), None)
        if not node:
            raise HTTPException(status_code=404, detail=f"Node {node_id} not found")
        return node.to_dict()

    @app.delete("/api/nodes/{node_id}")
    async def remove_node(node_id: str):
        """Remove a node from the registry."""
        nonlocal nodes
        original_len = len(nodes)
        nodes = [n for n in nodes if n.id != node_id]
        if len(nodes) == original_len:
            raise HTTPException(status_code=404, detail=f"Node {node_id} not found")
        logger.info(f"Removed node: {node_id}")
        return {"removed": node_id}

    # ─────────────────────────────────────────────────────────
    # Command Routing
    # ─────────────────────────────────────────────────────────

    @app.post("/api/command")
    async def send_command(command_data: dict):
        """Route command to device via best available transport.

        Payload: {dest: node_id, command: "GPIO 5 HIGH"}
        Response: {id: msg_id, status: "SENT" | "FAILED"}
        """
        dest = command_data.get("dest")
        command_str = command_data.get("command")

        if not dest or not command_str:
            raise HTTPException(status_code=400, detail="Both 'dest' and 'command' are required")

        # Find destination node
        node = next((n for n in nodes if n.id == dest), None)
        if not node:
            raise HTTPException(status_code=404, detail=f"Node {dest} not found")

        # Persist message before sending (queued state)
        msg = Message(dest=dest, command=command_str)
        message_queue.save_message(msg)

        # Route via transport manager
        try:
            success = await transport_manager.send_command(node, command_str)
            if success:
                message_queue.update_status(msg.id, MessageStatus.SENT)
                logger.info(f"Command sent to {node.name}: {command_str}")
                return {"id": msg.id, "status": "SENT"}
            else:
                message_queue.update_status(msg.id, MessageStatus.FAILED)
                logger.warning(f"Command failed for {node.name}: {command_str}")
                return {"id": msg.id, "status": "FAILED"}
        except Exception as e:
            logger.error(f"Command routing error for {dest}: {e}")
            message_queue.update_status(msg.id, MessageStatus.FAILED)
            raise HTTPException(status_code=500, detail=str(e))

    # ─────────────────────────────────────────────────────────
    # Provisioning (Phase 1: Modular Deployment)
    # ─────────────────────────────────────────────────────────

    @app.post("/api/provision")
    async def provision_device(req_data: dict):
        """Provision a device with carrier profile, product config, and identity.

        Payload: {device_id, carrier, product, identity, features, reboot}
        Response: {status, device_id, reboot_in_ms}
        """
        try:
            req = ProvisionRequest(**req_data)
        except TypeError as e:
            raise HTTPException(status_code=400, detail=f"Invalid provision request: {str(e)}")

        # Find device
        device = next((n for n in nodes if n.id == req.device_id), None)
        if not device:
            raise HTTPException(status_code=404, detail=f"Device {req.device_id} not found")

        # Load carrier profile
        carrier_profile = _load_carrier_profile(req.carrier)
        if not carrier_profile:
            raise HTTPException(status_code=400, detail=f"Carrier profile '{req.carrier}' not found")

        # Merge configs: carrier defaults + product overrides + explicit features
        merged_config = {
            "features": {**carrier_profile.features, **(req.features or {})},
            "hw": carrier_profile.hw,
            "mesh": {},
            "identity": req.identity or {},
        }

        # Build provisioning payload
        provision_payload = {
            "features": merged_config["features"],
            "hw": merged_config["hw"],
            "mesh": merged_config["mesh"],
            "identity": merged_config["identity"],
            "reboot": req.reboot,
        }

        # Send to device — WiFi nodes get direct HTTP POST to /api/provision,
        # BLE/Serial nodes get a PROVISION command string via transport fallback.
        try:
            import re as _re
            is_wifi = _re.match(r"^\d{1,3}(\.\d{1,3}){3}$", device.address)
            success = False

            if is_wifi and aiohttp:
                try:
                    import aiohttp as _aiohttp
                    async with _aiohttp.ClientSession() as session:
                        async with session.post(
                            f"http://{device.address}/api/provision",
                            json=provision_payload,
                            timeout=_aiohttp.ClientTimeout(total=5.0),
                        ) as resp:
                            success = resp.status == 200
                            if not success:
                                body = await resp.text()
                                logger.warning(f"Device provision HTTP {resp.status}: {body}")
                except Exception as e:
                    logger.warning(f"HTTP provision failed for {device.address}, trying transport fallback: {e}")

            if not success:
                success = await transport_manager.send_command(
                    device,
                    f"PROVISION {json.dumps(provision_payload)}"
                )

            if success:
                logger.info(f"Provisioned {req.device_id} with carrier={req.carrier}")
                return {
                    "status": "ok",
                    "device_id": req.device_id,
                    "reboot_in_ms": 2000 if req.reboot else None,
                }
            raise HTTPException(status_code=500, detail=f"Failed to send provision to {req.device_id}")
        except HTTPException:
            raise
        except Exception as e:
            logger.error(f"Provision error for {req.device_id}: {e}")
            raise HTTPException(status_code=500, detail=str(e))

    @app.get("/api/carriers")
    async def list_carriers():
        """List available carrier board profiles."""
        carriers_dir = Path(__file__).parent / "carriers"
        if not carriers_dir.exists():
            return []
        profiles = []
        for profile_file in carriers_dir.glob("*.json"):
            try:
                with open(profile_file) as f:
                    data = json.load(f)
                    profiles.append(data)
            except Exception as e:
                logger.warning(f"Failed to load carrier profile {profile_file}: {e}")
        return profiles

    def _load_carrier_profile(carrier_id: str) -> Optional[CarrierProfile]:
        """Load carrier profile from disk."""
        profile_path = Path(__file__).parent / "carriers" / f"{carrier_id}.json"
        if not profile_path.exists():
            return None
        try:
            with open(profile_path) as f:
                data = json.load(f)
                return CarrierProfile(
                    id=data.get("id", carrier_id),
                    name=data.get("name", ""),
                    hw=data.get("hw", {}),
                    features=data.get("features", {}),
                    pins=data.get("pins"),
                )
        except Exception as e:
            logger.error(f"Failed to load carrier profile {carrier_id}: {e}")
            return None

    # ─────────────────────────────────────────────────────────
    # Message History
    # ─────────────────────────────────────────────────────────

    @app.get("/api/messages")
    async def list_messages(status: Optional[str] = None, dest: Optional[str] = None):
        """List messages in the queue with optional filters."""
        msg_status = None
        if status:
            try:
                msg_status = MessageStatus(status)
            except ValueError:
                raise HTTPException(status_code=400, detail=f"Invalid status: {status}")

        messages = message_queue.list_messages(status=msg_status, dest=dest, limit=100)
        return [m.to_dict() for m in messages]

    @app.get("/api/messages/{msg_id}")
    async def get_message(msg_id: str):
        """Get message status by ID."""
        msg = message_queue.get_message(msg_id)
        if not msg:
            raise HTTPException(status_code=404, detail=f"Message {msg_id} not found")
        return msg.to_dict()

    # ─────────────────────────────────────────────────────────
    # Health Check
    # ─────────────────────────────────────────────────────────

    @app.get("/health")
    async def health_check():
        """Daemon liveness check."""
        return {
            "status": "ok",
            "nodes_count": len(nodes),
            "node_ids": [n.id for n in nodes]
        }

    return app
