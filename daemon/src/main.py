"""
LoRaLink Daemon: Central intelligence layer for Phase 50 Autonomous Mesh Sovereignty.

Purpose:
- Routes commands from webapp/clients to devices via mesh (direct/multi-hop/queue)
- Discovers and tracks device topology in real-time
- Handles device telemetry, status updates, and command ACKs
- Manages command queueing and retry for offline devices
- Provides REST API for mesh control and status monitoring

Architecture:
- MeshRouter: Core routing logic (peer discovery, path finding, queueing)
- MQTT Client: Listens for device status and handles acknowledgments
- REST API: FastAPI server for webapp integration
- Background Tasks: Periodic retry, queue draining, health checks

Usage:
    python main.py [--port 8001] [--mqtt-broker localhost:1883]
"""

import asyncio
import logging
import argparse
import signal
from typing import Optional
import time

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import uvicorn

from mesh_router import MeshRouter, MeshPeer, TransportType, MeshCommand
from mesh_api import init_mesh_api, health_check
import mqtt_client

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger(__name__)


class LoRaLinkDaemon:
    """Main daemon coordinator."""

    def __init__(self, port: int = 8001, mqtt_broker: str = "localhost:1883"):
        self.port = port
        self.mqtt_broker = mqtt_broker

        # Core components
        self.router = MeshRouter(own_node_id="daemon-0")
        self.mqtt = None
        self.app = None

        # Background tasks
        self.running = False
        self.background_tasks = []

    async def initialize(self) -> None:
        """Initialize daemon components."""
        logger.info("[Daemon] Initializing LoRaLink Daemon...")

        # Setup FastAPI app
        self.app = FastAPI(
            title="LoRaLink Daemon",
            description="Phase 50 Autonomous Mesh Sovereignty",
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

        # Mount mesh API
        mesh_api = init_mesh_api(self.router)
        self.app.include_router(mesh_api)

        # Add health check to root
        @self.app.get("/health")
        async def root_health():
            return {
                "status": "healthy",
                "service": "LoRaLink Daemon",
                "peers": len(self.router.peer_registry),
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

        logger.info("[Daemon] Initialization complete!")

    async def start(self) -> None:
        """Start the daemon."""
        logger.info("[Daemon] Starting services...")
        self.running = True

        # Start background tasks
        self.background_tasks = [
            asyncio.create_task(self._retry_loop()),
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
    # Device Event Handlers
    # ─────────────────────────────────────────────────────────────────

    async def _handle_device_status(self, node_id: str, status: dict) -> None:
        """Process device status update from MQTT."""
        logger.info(f"[Status] {node_id}: {status}")

        # Get or create peer
        peer = self.router.get_peer(node_id)
        if not peer:
            # New device discovered
            peer = MeshPeer(
                node_id=node_id,
                mac_address=status.get("mac", "00:00:00:00:00:00"),
                last_seen=time.time() * 1000,
                rssi_dbm=status.get("rssi", -80),
                transport=TransportType.MQTT,  # Default to MQTT for status messages
                reachable=True,
                neighbors=status.get("neighbors", []),
            )
            self.router.register_peer(peer)
        else:
            # Update existing peer
            self.router.update_peer_status(node_id, status)

    async def _handle_command_ack(self, cmd_id: str, success: bool, result: dict) -> None:
        """Process command acknowledgment from device."""
        logger.info(f"[Ack] {cmd_id}: {'SUCCESS' if success else 'FAILED'}")
        self.router.handle_command_ack(cmd_id, success, result)

    # ─────────────────────────────────────────────────────────────────
    # Background Tasks
    # ─────────────────────────────────────────────────────────────────

    async def _retry_loop(self) -> None:
        """Background task: retry queued commands periodically."""
        logger.info("[Background] Retry loop started (30s interval)")

        while self.running:
            try:
                await asyncio.sleep(30)  # Retry every 30 seconds
                self.router.retry_failed_commands(max_retries=3)
            except Exception as e:
                logger.error(f"[Retry] Error: {e}")

    async def _health_loop(self) -> None:
        """Background task: periodic health checks and diagnostics."""
        logger.info("[Background] Health loop started (60s interval)")

        while self.running:
            try:
                await asyncio.sleep(60)  # Health check every 60 seconds
                stats = self.router.get_stats()
                logger.info(
                    f"[Health] Peers={stats['peer_count']}, "
                    f"Pending={stats['pending_commands']}, "
                    f"Queued={stats['queued_commands']}"
                )

                # Clean up stale peers
                stale_peers = [
                    p for p in self.router.list_peers()
                    if p.is_stale(threshold_ms=120000)  # 2 minutes
                ]
                for peer in stale_peers:
                    logger.info(f"[Health] Marking {peer.node_id} as stale (no update for 2min)")
                    peer.reachable = False

            except Exception as e:
                logger.error(f"[Health] Error: {e}")


async def main():
    """Entry point."""
    parser = argparse.ArgumentParser(description="LoRaLink Daemon")
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
