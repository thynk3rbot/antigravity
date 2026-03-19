"""
RAG Router -- Universal IoT-to-RAG Routing Microservice
Pipeline: MQTT Ingest -> Domain Classification -> Completeness Guard -> Dify RAG -> Response
Follows the PeripheralInfo schema from DataManager.h exactly.
"""

import json, logging, os, time
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional

import httpx
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

load_dotenv()

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR = os.path.join(BASE_DIR, "static")

DIFY_BASE_URL = os.getenv("DIFY_BASE_URL", "http://localhost:5001/v1")
DIFY_API_KEY = os.getenv("DIFY_API_KEY", "")
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")
RAG_ROUTER_PORT = int(os.getenv("RAG_ROUTER_PORT", "8200"))
LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO")

logging.basicConfig(level=getattr(logging, LOG_LEVEL), format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("rag_router")

# -- Domain Classification (fuzzy hwType matching) ----------------------------

DOMAIN_MAP = {
    "NUTRIENT": ["ph", "ec", "tds", "ppm", "nutrient", "dose", "pump", "reservoir"],
    "BOTANICAL": ["dht", "humidity", "soil", "moisture", "light", "par", "temp", "co2", "vpd"],
    "HARDWARE": ["relay", "valve", "fan", "motor", "flow", "level", "pressure", "mcp"],
}

DOMAIN_REQUIRED_KEYS = {
    "NUTRIENT": ["ph", "ec"],
    "BOTANICAL": ["temp"],
    "HARDWARE": [],
}

EXPERT_PROFILES = {
    "NUTRIENT": (
        "You are a hydroponic nutrient analyst trained on the Bruce Bugbee/USU protocols. "
        "Analyze the sensor readings strictly against the Utah Hydroponic Solution reference data. "
        "If pH < 5.5 or pH > 6.5, explain the specific nutrient lockout mechanism occurring. "
        "If EC is outside 1.0-2.5 mS/cm range, explain the osmotic stress implications. "
        "Never guess -- only cite data from the knowledge base."
    ),
    "BOTANICAL": (
        "You are an environmental botanist. Analyze temperature, humidity, and light readings "
        "against optimal crop ranges from the knowledge base. Report VPD calculations if "
        "temperature and humidity are both available."
    ),
    "HARDWARE": (
        "You are a LoRaLink hardware diagnostics specialist. Analyze relay states, flow rates, "
        "and device telemetry. Flag any readings outside normal operating parameters."
    ),
}


def classify_domain(hw_type, sensor_id):
    combined = f"{hw_type} {sensor_id}".lower()
    for domain, keywords in DOMAIN_MAP.items():
        if any(kw in combined for kw in keywords):
            return domain
    return "HARDWARE"


def get_knowledge_id(domain):
    return os.getenv(f"KNOWLEDGE_ID_{domain}", "")


def check_completeness(domain, readings):
    """Relentless Honesty guard -- refuse analysis if required keys missing."""
    required = DOMAIN_REQUIRED_KEYS.get(domain, [])
    present_keys = {k.lower() for k in readings.keys()}
    missing = [k for k in required if k not in present_keys]
    if missing:
        return (
            f"INSUFFICIENT DATA FOR {domain} ANALYSIS. "
            f"Missing required sensor(s): {', '.join(missing).upper()}. "
            f"Present: {', '.join(sorted(present_keys)).upper() or 'NONE'}. "
            f"Cannot perform analysis without complete data -- refusing to guess."
        )
    return None


# -- Dify RAG Client ----------------------------------------------------------

async def query_dify(query_text, domain, conversation_id=""):
    if not DIFY_API_KEY:
        return {"answer": "RAG ENGINE NOT CONFIGURED: Set DIFY_API_KEY in .env", "sources": []}
    expert_prompt = EXPERT_PROFILES.get(domain, "Analyze the following sensor data.")
    full_query = f"{expert_prompt}\n\nSensor Data:\n{query_text}"
    headers = {"Authorization": f"Bearer {DIFY_API_KEY}", "Content-Type": "application/json"}
    payload = {"inputs": {"domain": domain}, "query": full_query, "response_mode": "blocking", "user": "rag-router"}
    if conversation_id:
        payload["conversation_id"] = conversation_id
    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            resp = await client.post(f"{DIFY_BASE_URL}/chat-messages", headers=headers, json=payload)
            resp.raise_for_status()
            data = resp.json()
            return {
                "answer": data.get("answer", "No answer returned from RAG engine."),
                "conversation_id": data.get("conversation_id", ""),
                "sources": data.get("metadata", {}).get("retriever_resources", []),
            }
    except httpx.HTTPStatusError as e:
        log.error(f"Dify API error: {e.response.status_code}")
        return {"answer": f"RAG ENGINE ERROR: HTTP {e.response.status_code}", "sources": []}
    except Exception as e:
        log.error(f"Dify connection failed: {e}")
        return {"answer": f"RAG ENGINE UNREACHABLE: {e}", "sources": []}


# -- Sensor State Aggregator (maps to DataManager.h PeripheralInfo) -----------

@dataclass
class NodeState:
    node_id: str
    hw_type: str = "unknown"
    readings: dict = field(default_factory=dict)
    last_updated: float = 0.0
    caps: str = ""


class SensorAggregator:
    def __init__(self):
        self.nodes = {}

    def update(self, node_id, key, value):
        if node_id not in self.nodes:
            self.nodes[node_id] = NodeState(node_id=node_id)
        node = self.nodes[node_id]
        try:
            node.readings[key] = float(value)
        except ValueError:
            node.readings[key] = value
        node.last_updated = time.time()
        return node

    def update_from_peripheral(self, peripheral):
        """Ingest a PeripheralInfo JSON (maps to DataManager.h struct)."""
        node_id = peripheral.get("id", "unknown")
        if node_id not in self.nodes:
            self.nodes[node_id] = NodeState(node_id=node_id)
        node = self.nodes[node_id]
        node.hw_type = peripheral.get("hw", peripheral.get("hwType", "unknown"))
        node.caps = peripheral.get("caps", "")
        data = peripheral.get("data", peripheral.get("lastReadings", {}))
        if isinstance(data, str):
            try:
                data = json.loads(data)
            except json.JSONDecodeError:
                data = {}
        for k, v in data.items():
            try:
                node.readings[k] = float(v)
            except (ValueError, TypeError):
                node.readings[k] = v
        node.last_updated = time.time()
        return node

    def get_node(self, node_id):
        return self.nodes.get(node_id)

    def get_all(self):
        return [
            {"node_id": n.node_id, "hw_type": n.hw_type, "readings": n.readings,
             "age_seconds": round(time.time() - n.last_updated, 1) if n.last_updated else None}
            for n in self.nodes.values()
        ]


aggregator = SensorAggregator()


@dataclass
class QueryRecord:
    timestamp: str
    node_id: str
    domain: str
    readings: dict
    query: str
    answer: str
    sources: list
    guard_rejected: bool = False


query_history = []
MAX_HISTORY = 100
ws_clients = set()


async def broadcast(event, data):
    msg = json.dumps({"event": event, "data": data, "ts": datetime.now(timezone.utc).isoformat()})
    stale = set()
    for ws in ws_clients:
        try:
            await ws.send_text(msg)
        except Exception:
            stale.add(ws)
    ws_clients -= stale


# -- Full RAG Pipeline ---------------------------------------------------------

async def run_pipeline(node_id, query_override=""):
    node = aggregator.get_node(node_id)
    if not node:
        return {"error": "No data for node. Register via SENSOR command first.", "node_id": node_id}

    domain = classify_domain(node.hw_type, node.node_id)

    guard_msg = check_completeness(domain, node.readings)
    if guard_msg:
        record = QueryRecord(
            timestamp=datetime.now(timezone.utc).isoformat(), node_id=node_id, domain=domain,
            readings=dict(node.readings), query=query_override or "auto",
            answer=guard_msg, sources=[], guard_rejected=True)
        query_history.append(record)
        if len(query_history) > MAX_HISTORY:
            query_history.pop(0)
        await broadcast("guard_rejection", {"node_id": node_id, "domain": domain, "message": guard_msg})
        return {"domain": domain, "answer": guard_msg, "guard_rejected": True}

    readings_text = "\n".join(f"  {k}: {v}" for k, v in sorted(node.readings.items()))
    query_text = query_override or f"Node: {node_id} (hwType: {node.hw_type})\nReadings:\n{readings_text}"

    log.info(f"RAG query: node={node_id} domain={domain}")
    result = await query_dify(query_text, domain)

    record = QueryRecord(
        timestamp=datetime.now(timezone.utc).isoformat(), node_id=node_id, domain=domain,
        readings=dict(node.readings), query=query_text,
        answer=result.get("answer", ""), sources=result.get("sources", []))
    query_history.append(record)
    if len(query_history) > MAX_HISTORY:
        query_history.pop(0)
    await broadcast("rag_response", {
        "node_id": node_id, "domain": domain,
        "answer": result["answer"], "sources": result.get("sources", [])})
    return {"domain": domain, **result}


# -- MQTT Client ---------------------------------------------------------------

mqtt_client_ref = None


def on_mqtt_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        log.info("MQTT connected -- subscribing to sensor topics")
        client.subscribe("loralink/+/sensor/+", qos=1)
        client.subscribe("loralink/telemetry/+", qos=1)
    else:
        log.error(f"MQTT connection failed: rc={rc}")


def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")
    parts = topic.split("/")
    if len(parts) == 4 and parts[2] == "sensor":
        aggregator.update(parts[1], parts[3], payload)
    elif len(parts) == 3 and parts[1] == "telemetry":
        try:
            data = json.loads(payload)
            for k, v in data.items():
                if k != "node":
                    aggregator.update(parts[2], k, str(v))
        except json.JSONDecodeError:
            pass


def start_mqtt():
    global mqtt_client_ref
    mqtt_client_ref = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="rag-router")
    if MQTT_USER:
        mqtt_client_ref.username_pw_set(MQTT_USER, MQTT_PASS)
    mqtt_client_ref.on_connect = on_mqtt_connect
    mqtt_client_ref.on_message = on_mqtt_message
    try:
        mqtt_client_ref.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client_ref.loop_start()
        log.info(f"MQTT connecting to {MQTT_BROKER}:{MQTT_PORT}")
    except Exception as e:
        log.warning(f"MQTT unavailable ({e}) -- running in manual-query mode only")


def stop_mqtt():
    if mqtt_client_ref:
        mqtt_client_ref.loop_stop()
        mqtt_client_ref.disconnect()


# -- FastAPI App ---------------------------------------------------------------

@asynccontextmanager
async def lifespan(app):
    start_mqtt()
    yield
    stop_mqtt()


app = FastAPI(title="RAG Router", version="0.1.0", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


class ManualQuery(BaseModel):
    node_id: str
    query: str = ""

class PeripheralIngest(BaseModel):
    id: str
    hw: str = "unknown"
    hwType: str = ""
    caps: str = ""
    data: dict = {}
    lastReadings: str = ""

class ProductRegistration(BaseModel):
    domain: str
    knowledge_id: str
    required_keys: list = []
    keywords: list = []
    expert_prompt: str = ""


@app.get("/")
async def index():
    return FileResponse(os.path.join(STATIC_DIR, "index.html"))

@app.get("/health")
async def health():
    return {"status": "ok", "service": "rag_router"}

@app.get("/test")
async def test_page():
    return FileResponse(os.path.join(STATIC_DIR, "test.html"))

@app.get("/api/nodes")
async def get_nodes():
    return aggregator.get_all()

@app.get("/api/history")
async def get_history(limit: int = 20):
    return [
        {"timestamp": r.timestamp, "node_id": r.node_id, "domain": r.domain,
         "readings": r.readings, "query": r.query[:200], "answer": r.answer,
         "sources": r.sources, "guard_rejected": r.guard_rejected}
        for r in reversed(query_history[-limit:])
    ]

@app.post("/api/query")
async def manual_query(req: ManualQuery):
    return await run_pipeline(req.node_id, req.query)

@app.post("/api/ingest")
async def ingest_peripheral(peripheral: PeripheralIngest):
    pdata = peripheral.model_dump()
    if peripheral.hwType:
        pdata["hw"] = peripheral.hwType
    if not pdata["data"] and pdata["lastReadings"]:
        try:
            pdata["data"] = json.loads(pdata["lastReadings"])
        except json.JSONDecodeError:
            pass
    node = aggregator.update_from_peripheral(pdata)
    return {"status": "ingested", "node_id": node.node_id, "readings": node.readings}

@app.post("/api/ingest-and-query")
async def ingest_and_query(peripheral: PeripheralIngest):
    pdata = peripheral.model_dump()
    if peripheral.hwType:
        pdata["hw"] = peripheral.hwType
    if not pdata["data"] and pdata["lastReadings"]:
        try:
            pdata["data"] = json.loads(pdata["lastReadings"])
        except json.JSONDecodeError:
            pass
    aggregator.update_from_peripheral(pdata)
    return await run_pipeline(peripheral.id)

@app.post("/api/products/register")
async def register_product(reg: ProductRegistration):
    domain = reg.domain.upper()
    os.environ[f"KNOWLEDGE_ID_{domain}"] = reg.knowledge_id
    if reg.keywords:
        DOMAIN_MAP[domain] = reg.keywords
    if reg.required_keys:
        DOMAIN_REQUIRED_KEYS[domain] = reg.required_keys
    if reg.expert_prompt:
        EXPERT_PROFILES[domain] = reg.expert_prompt
    return {"status": "registered", "domain": domain}

@app.get("/api/domains")
async def list_domains():
    return {
        domain: {
            "keywords": DOMAIN_MAP.get(domain, []),
            "required_keys": DOMAIN_REQUIRED_KEYS.get(domain, []),
            "knowledge_id": get_knowledge_id(domain),
            "has_expert_prompt": domain in EXPERT_PROFILES,
        }
        for domain in set(list(DOMAIN_MAP.keys()) + list(EXPERT_PROFILES.keys()))
    }

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_clients.add(ws)
    try:
        while True:
            data = await ws.receive_text()
            try:
                msg = json.loads(data)
                if msg.get("action") == "query":
                    result = await run_pipeline(msg["node_id"], msg.get("query", ""))
                    await ws.send_text(json.dumps({"event": "query_result", "data": result}))
            except (json.JSONDecodeError, KeyError):
                await ws.send_text(json.dumps({"event": "error", "data": {"message": "Invalid JSON"}}))
    except WebSocketDisconnect:
        ws_clients.discard(ws)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=RAG_ROUTER_PORT)
