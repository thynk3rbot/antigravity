import json
import os
import sys
import yaml
import asyncio
from pathlib import Path
from datetime import datetime

# Path boilerplate to ensure local imports work when run from root
current_dir = Path(__file__).resolve().parent
if str(current_dir) not in sys.path:
    sys.path.insert(0, str(current_dir))

import paho.mqtt.client as mqtt
from fastapi import FastAPI, Query, WebSocket, Request
from fastapi.middleware.cors import CORSMiddleware
import uvicorn
from monitor import monitor
from persistence import persistence

class MagicLVCService:
    def __init__(self, config_path=None, rest_port=8200):
        self.config_path = config_path or os.getenv("MAGIC_CACHE_CONFIG", str(current_dir / "config.yaml"))
        self.config = self._load_config()
        self.lvc_store = {}  # In-memory "Magic" image
        self.rest_port = rest_port
        self.alerts = {}  # Alert management: {alert_id: {name, condition, enabled, triggered}}
        self.subscribers = []  # WebSocket subscribers for real-time updates

        # MQTT Setup (paho-mqtt 2.1 compatible)
        try:
            from paho.mqtt.enums import CallbackAPIVersion
            self.client = mqtt.Client(CallbackAPIVersion.VERSION2, "MagicLVCService")
        except ImportError:
            # Fallback for older paho-mqtt < 2.0
            self.client = mqtt.Client("MagicLVCService")
        
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

        # FastAPI Setup
        self.app = FastAPI(
            title="Magic Cache LVC",
            description="Real-time time-series cache for Magic mesh",
            version="1.0.0"
        )
        self.app.add_middleware(
            CORSMiddleware,
            allow_origins=["*"],
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )
        self._setup_routes()

    def _load_config(self):
        try:
            with open(self.config_path, 'r') as f:
                return yaml.safe_load(f)
        except Exception as e:
            monitor.log_error("LVC", f"Config load failed: {e}")
            return {}

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            monitor.log_info("Bus", "Connected to Magic Bus (MQTT)")
            prefix = self.config.get('magic_bus', {}).get('topic_prefix', 'MagicCache')
            client.subscribe(f"{prefix}/#")
            monitor.log_info("Bus", f"Subscribed to {prefix}/#")
        else:
            monitor.log_error("Bus", f"Connection failed with code {rc}")

    def on_message(self, client, userdata, msg):
        """Handle incoming KV data (JSON)."""
        try:
            topic = msg.topic
            subject = topic.split('/', 1)[-1].replace('/', '.')
            payload = json.loads(msg.payload.decode())
            
            if not isinstance(payload, dict):
                monitor.log_error("LVC", f"Invalid payload (not KV): {payload}")
                return

            update_type = "INSERT" if subject not in self.lvc_store else "REPLACE"
            
            if subject not in self.lvc_store:
                self.lvc_store[subject] = {}
            
            self.lvc_store[subject].update(payload)
            monitor.log_update(subject, update_type, len(payload))
            
            if self.config.get('persistence', {}).get('write_through', True):
                persistence.sync_record(subject, self.lvc_store[subject])

        except Exception as e:
            monitor.log_error("LVC", f"Message processing failed: {e}")

    def _setup_routes(self):
        """Setup FastAPI REST endpoints for MagicCache."""

        @self.app.get("/health")
        async def health():
            """Health check endpoint."""
            return {
                "status": "healthy",
                "service": "magic-lvc",
                "tables": len(self.lvc_store),
                "timestamp": datetime.utcnow().isoformat()
            }

        @self.app.get("/tables")
        async def list_tables():
            """List all tables in the cache."""
            return {
                "tables": list(self.lvc_store.keys()),
                "count": len(self.lvc_store)
            }

        @self.app.get("/tables/{table_name}")
        async def get_table(table_name: str):
            """Get all records from a specific table."""
            if table_name not in self.lvc_store:
                return {"error": f"Table '{table_name}' not found"}, 404
            return {
                "table": table_name,
                "records": self.lvc_store[table_name],
                "count": len(self.lvc_store[table_name])
            }

        @self.app.get("/query")
        async def query(table: str = Query(...), filters: str = Query(None)):
            """Query data with optional JSON filters."""
            if table not in self.lvc_store:
                return {"error": f"Table '{table}' not found"}, 404

            results = self.lvc_store[table]

            if filters:
                try:
                    filter_dict = json.loads(filters)
                    results = {k: v for k, v in results.items()
                               if all(v.get(fk) == fv for fk, fv in filter_dict.items())}
                except json.JSONDecodeError:
                    return {"error": "Invalid JSON filters"}, 400

            return {
                "table": table,
                "filters": filters,
                "results": results,
                "count": len(results)
            }

        @self.app.post("/export")
        async def export(table: str = Query(...), format: str = Query("json")):
            """Export table data in specified format."""
            if table not in self.lvc_store:
                return {"error": f"Table '{table}' not found"}, 404

            data = self.lvc_store[table]
            timestamp = datetime.utcnow().strftime("%Y%m%d_%H%M%S")

            if format == "json":
                return {
                    "format": "json",
                    "table": table,
                    "data": data,
                    "export_timestamp": timestamp
                }
            elif format == "csv":
                # Simple CSV export
                import csv
                import io
                output = io.StringIO()
                if data and isinstance(next(iter(data.values())), dict):
                    writer = csv.DictWriter(output, fieldnames=set().union(*[d.keys() for d in data.values()]))
                    writer.writeheader()
                    for record in data.values():
                        writer.writerow(record)
                return {
                    "format": "csv",
                    "table": table,
                    "data": output.getvalue(),
                    "export_timestamp": timestamp
                }
            else:
                return {"error": f"Unsupported format: {format}"}, 400

        # ── Alert Management Endpoints ──
        @self.app.get("/alerts")
        async def list_alerts():
            """List all configured alerts."""
            return {
                "alerts": self.alerts,
                "count": len(self.alerts)
            }

        @self.app.post("/alerts")
        async def create_alert(request):
            """Create a new alert rule."""
            body = await request.json()
            alert_id = body.get("id", f"alert_{len(self.alerts)+1}")
            self.alerts[alert_id] = {
                "name": body.get("name", "Unnamed Alert"),
                "condition": body.get("condition", ""),  # e.g. "battery < 20"
                "enabled": body.get("enabled", True),
                "triggered": False,
                "last_check": None
            }
            return {"ok": True, "alert_id": alert_id}

        @self.app.put("/alerts/{alert_id}")
        async def update_alert(alert_id: str, request):
            """Update an alert rule."""
            if alert_id not in self.alerts:
                return {"error": f"Alert '{alert_id}' not found"}, 404
            body = await request.json()
            self.alerts[alert_id].update(body)
            return {"ok": True, "alert_id": alert_id}

        @self.app.delete("/alerts/{alert_id}")
        async def delete_alert(alert_id: str):
            """Delete an alert rule."""
            if alert_id not in self.alerts:
                return {"error": f"Alert '{alert_id}' not found"}, 404
            del self.alerts[alert_id]
            return {"ok": True}

        # ── WebSocket Subscription ──
        @self.app.websocket("/ws")
        async def websocket_endpoint(websocket: WebSocket):
            """WebSocket endpoint for real-time data subscriptions."""
            await websocket.accept()
            self.subscribers.append(websocket)
            try:
                while True:
                    data = await websocket.receive_text()
                    # Echo back subscription confirmation
                    await websocket.send_json({
                        "type": "subscription",
                        "message": f"Subscribed to: {data}"
                    })
            except Exception as e:
                monitor.log_error("WS", f"WebSocket error: {e}")
            finally:
                self.subscribers.remove(websocket)

        @self.app.post("/broadcast")
        async def broadcast_update(request: Request):
            """Broadcast a message to all WebSocket subscribers."""
            body = await request.json()
            for subscriber in self.subscribers:
                try:
                    await subscriber.send_json({
                        "type": "update",
                        "data": body
                    })
                except Exception as e:
                    monitor.log_error("WS", f"Broadcast error: {e}")

    def run(self):
        import threading

        # Start FastAPI server in a background thread
        def run_fastapi():
            uvicorn.run(
                self.app,
                host="0.0.0.0",
                port=self.rest_port,
                log_level="info"
            )

        api_thread = threading.Thread(target=run_fastapi, daemon=True)
        api_thread.start()
        monitor.log_info("LVC", f"REST API started on port {self.rest_port}")

        # Run MQTT client on main thread
        mqtt_host = self.config.get('magic_bus', {}).get('host', 'localhost')
        mqtt_port = self.config.get('magic_bus', {}).get('port', 1883)
        try:
            self.client.connect(mqtt_host, mqtt_port, 60)
            monitor.log_info("LVC", "Service Active (Awaiting Magic Updates)")
            self.client.loop_forever()
        except Exception as e:
            monitor.log_error("LVC", f"Runtime Error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    service = MagicLVCService()
    service.run()
