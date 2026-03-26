"""
LoRaLink Daemon: Pure Mesh Gateway for Phase 50 Autonomous Mesh Sovereignty.

Pure Mesh Architecture:
- Devices mesh with each other via ControlPacket (LoRa/BLE/ESP-NOW)
- All devices understand all capabilities (relay even if they don't have them)
- Daemon monitors topology (reads MQTT status)
- Daemon acts as command gateway (publishes to MQTT for devices to relay)
- Webapp controls mesh through daemon REST API

Key: Devices own the mesh routing. Daemon owns the interface.

Usage:
    python run.py [--port 8001] [--mqtt-broker localhost:1883]
"""

import asyncio
import json
import logging
import argparse
import signal
from pathlib import Path
from typing import Optional

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import uvicorn

try:
    from .mesh_router import MeshTopology
    from .mesh_api import init_mesh_api
    from .service_manager import ServiceManager
    from .community import CommunityManager
    from . import mqtt_client
except ImportError:
    from mesh_router import MeshTopology
    from mesh_api import init_mesh_api
    from service_manager import ServiceManager
    from community import CommunityManager
    import mqtt_client

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)


class LoRaLinkDaemon:
    """
    LoRaLink Daemon — the octopus.

    Central hub for all system services:
    - Manages service lifecycle (webapp, assistant, ollama_proxy, + any in config.json)
    - Mesh gateway: monitors fleet topology via MQTT
    - Command gateway: injects commands into the mesh
    - OTA firmware delivery: runs pio espota against device IPs
    - REST API on port 8001 for all of the above

    Each service runs standalone as a subprocess. The daemon starts, stops,
    restarts, and tails logs for all of them. New services: add to daemon/config.json.
    """

    def __init__(self, port: int = 8001, mqtt_broker: str = "localhost:1883"):
        self.port = port
        self.mqtt_broker = mqtt_broker

        # Core components
        self._config_path = Path(__file__).parent.parent / "config.json"
        self.topology = MeshTopology(own_node_id="daemon-0")
        self.mqtt = None
        self.app = None
        self.services = ServiceManager()
        self.community = CommunityManager(self._config_path)

        # Background tasks
        self.running = False
        self.background_tasks = []

    async def initialize(self) -> None:
        """Initialize daemon components."""
        logger.info("[Daemon] Initializing LoRaLink Daemon (Pure Mesh Gateway)...")

        # Setup FastAPI app
        self.app = FastAPI(
            title="LoRaLink Daemon",
            description="The octopus — central hub for fleet, firmware, and services",
            version="0.1.0",
        )

        # Add CORS middleware for webapp
        self.app.add_middleware(
            CORSMiddleware,
            allow_origins=["*"],
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )

        # Mount mesh API (pass MQTT publisher for command injection)
        mesh_api = init_mesh_api(self.topology, self.mqtt)
        self.app.include_router(mesh_api)

        # Mount service management API
        from fastapi import APIRouter
        svc_router = APIRouter(prefix="/api/services", tags=["services"])
        svc_mgr = self.services

        @svc_router.get("")
        async def list_services():
            return svc_mgr.status_all()

        @svc_router.post("/{name}/start")
        async def start_service(name: str):
            return await svc_mgr.start(name)

        @svc_router.post("/{name}/stop")
        async def stop_service(name: str):
            return await svc_mgr.stop(name)

        @svc_router.post("/{name}/restart")
        async def restart_service(name: str):
            return await svc_mgr.restart(name)

        @svc_router.get("/{name}/logs")
        async def service_logs(name: str, lines: int = 50):
            return {"name": name, "logs": svc_mgr.logs(name, lines)}

        self.app.include_router(svc_router)

        # ── Config API — read/write daemon/config.json live ──────────────
        from fastapi import Request as FRequest
        cfg_path = self._config_path
        community_mgr = self.community

        @self.app.get("/api/config")
        async def get_config():
            if cfg_path.exists():
                return json.loads(cfg_path.read_text())
            return {}

        @self.app.put("/api/config")
        async def save_config(request: FRequest):
            body = await request.json()
            cfg_path.write_text(json.dumps(body, indent=2))
            # Hot-reload services and community
            svc_mgr._load_services()
            community_mgr.reload()
            return {"ok": True, "message": "Config saved. Services and community reloaded."}

        # ── Community API — peer daemon registry ─────────────────────────
        @self.app.get("/api/community")
        async def get_community():
            return {
                "peers": community_mgr.status_all(),
                "total": community_mgr.peer_count(),
                "online": community_mgr.online_count(),
            }

        # Root health — full system status (visible to peer daemons polling us)
        @self.app.get("/health")
        async def root_health():
            svc_status = svc_mgr.status_all()
            running = [n for n, s in svc_status.items() if s["running"]]
            topo = self.topology.get_topology() if hasattr(self.topology, 'get_topology') else {}
            return {
                "status": "healthy",
                "daemon": "octopus",
                "peers": len(self.topology.peer_registry),
                "peers_online": topo.get("online_count", 0),
                "services_running": running,
                "services_total": len(svc_status),
                "community_peers": community_mgr.peer_count(),
                "community_online": community_mgr.online_count(),
            }

        # Initialize MQTT client
        logger.info(f"[Daemon] Connecting to MQTT broker at {self.mqtt_broker}...")
        self.mqtt = mqtt_client.MQTTClientManager(
            broker=self.mqtt_broker,
            on_device_status=self._handle_device_status,
            on_command_ack=self._handle_command_ack,
        )

        await self.mqtt.connect()
        await self.mqtt.subscribe_to_device_topics()

        # Update API with MQTT publisher
        init_mesh_api(self.topology, self.mqtt)

        # Start managed services (auto=true in config.json)
        await self.services.start_auto_services()

        # Start community polling (peer daemons)
        self.community.start()

        logger.info("[Daemon] Octopus initialized.")
        logger.info("  - Topology monitoring: ENABLED")
        logger.info("  - Command gateway: ENABLED")
        logger.info("  - Service manager: ENABLED")
        svc_status = self.services.status_all()
        for name, s in svc_status.items():
            icon = "✓" if s["running"] else "○"
            auto = " (auto)" if s["auto"] else ""
            logger.info(f"  {icon} {name}{auto} — {s['description']}")

    async def start(self) -> None:
        """Start the daemon."""
        logger.info("[Daemon] Starting services...")
        self.running = True

        # Start background tasks
        self.background_tasks = [
            asyncio.create_task(self._health_loop()),
        ]

        # Start FastAPI server
        config = uvicorn.Config(
            app=self.app,
            host="0.0.0.0",
            port=self.port,
            log_level="info",
        )
        server = uvicorn.Server(config)

        try:
            await server.serve()
        except Exception as e:
            logger.error(f"[Daemon] Server error: {e}")
            self.running = False

    async def shutdown(self) -> None:
        """Shutdown daemon gracefully."""
        logger.info("[Daemon] Shutting down...")
        self.running = False

        # Cancel background tasks
        for task in self.background_tasks:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        # Disconnect MQTT
        if self.mqtt:
            await self.mqtt.disconnect()

        logger.info("[Daemon] Shutdown complete!")

    # ─────────────────────────────────────────────────────────────────
    # MQTT Event Handlers
    # ─────────────────────────────────────────────────────────────────

    async def _handle_device_status(self, node_id: str, status: dict) -> None:
        """Process device status update from MQTT."""
        logger.debug(f"[Status] {node_id}: {status}")
        self.topology.update_peer_status(node_id, status)

    async def _handle_command_ack(self, cmd_id: str, success: bool, result: dict) -> None:
        """Process command acknowledgment from device."""
        logger.info(f"[Ack] {cmd_id}: {'SUCCESS' if success else 'FAILED'}")
        self.topology.handle_command_ack(cmd_id, success, result)

    # ─────────────────────────────────────────────────────────────────
    # Background Tasks
    # ─────────────────────────────────────────────────────────────────

    async def _health_loop(self) -> None:
        """Background task: periodic health checks and topology maintenance."""
        logger.info("[Background] Health loop started (60s interval)")

        while self.running:
            try:
                await asyncio.sleep(60)

                stats = self.topology.get_stats()
                logger.info(
                    f"[Health] Peers: {stats['total_peers']} total, "
                    f"{stats['online_peers']} online | "
                    f"Commands: {stats['active_commands']} active, "
                    f"{stats['command_history_size']} history"
                )

                # Mark stale peers as offline
                for peer in self.topology.list_peers():
                    if peer.is_stale(threshold_ms=120000):  # 2 minutes
                        peer.reachable = False
                        logger.info(f"[Health] Marking {peer.node_id} as offline (stale)")

            except Exception as e:
                logger.error(f"[Health] Error: {e}")


async def main():
    """Entry point."""
    parser = argparse.ArgumentParser(description="LoRaLink Pure Mesh Gateway Daemon")
    parser.add_argument(
        "--port", type=int, default=8001, help="REST API port (default: 8001)"
    )
    parser.add_argument(
        "--mqtt-broker",
        type=str,
        default="localhost:1883",
        help="MQTT broker address (default: localhost:1883)",
    )
    parser.add_argument(
        "--log-level",
        type=str,
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging level",
    )

    args = parser.parse_args()

    # Set logging level
    logging.getLogger().setLevel(getattr(logging, args.log_level))

    # Create and start daemon
    daemon = LoRaLinkDaemon(port=args.port, mqtt_broker=args.mqtt_broker)

    # Handle signals
    def signal_handler(sig, frame):
        logger.info("Received signal, shutting down...")
        asyncio.create_task(daemon.shutdown())

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        await daemon.initialize()
        await daemon.start()
    except Exception as e:
        logger.error(f"Fatal error: {e}")
        await daemon.shutdown()
        raise


if __name__ == "__main__":
    asyncio.run(main())
