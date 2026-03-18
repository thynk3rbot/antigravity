"""
NutriCalc Server — FastAPI backend.
Same structural patterns as tools/webapp/server.py (LoRaLink).
Run: uvicorn server:app --reload --port 8100
"""

import json
import sqlite3
import asyncio
import logging
from pathlib import Path
from datetime import datetime
from contextlib import asynccontextmanager
from typing import Optional

import paho.mqtt.client as mqtt
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from solver import solve_formula, reverse_solve, estimate_ec, ELEMENTS
from price_scraper import PriceScraper

# ── Paths ─────────────────────────────────────────────────────────────────────
BASE_DIR = Path(__file__).parent
STATIC_DIR = BASE_DIR / "static"
CHEMICALS_FILE = BASE_DIR / "chemicals.json"
DB_FILE = BASE_DIR / "nutribuddy.db"
MQTT_CONFIG_FILE = BASE_DIR / "mqtt_config.json"

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("nutribuddy")


# ── Database ──────────────────────────────────────────────────────────────────
def get_db():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with get_db() as db:
        db.execute("""
            CREATE TABLE IF NOT EXISTS formulas (
                id       INTEGER PRIMARY KEY AUTOINCREMENT,
                name     TEXT NOT NULL,
                notes    TEXT,
                created  TEXT NOT NULL,
                modified TEXT NOT NULL,
                data     TEXT NOT NULL
            )
        """)
        db.execute("""
            CREATE TABLE IF NOT EXISTS custom_compounds (
                id       INTEGER PRIMARY KEY AUTOINCREMENT,
                data     TEXT NOT NULL
            )
        """)
        db.commit()


# ── Chemical DB loader ────────────────────────────────────────────────────────
def load_chemicals():
    with open(CHEMICALS_FILE, encoding="utf-8") as f:
        return json.load(f)


def get_all_compounds():
    """Merge built-in compounds with user custom compounds."""
    chem_db = load_chemicals()
    compounds = list(chem_db["compounds"])
    with get_db() as db:
        for row in db.execute("SELECT data FROM custom_compounds"):
            compounds.append(json.loads(row["data"]))
    return compounds


# ── MQTT client ───────────────────────────────────────────────────────────────
class MQTTState:
    client: Optional[mqtt.Client] = None
    connected: bool = False
    config: dict = {}
    last_publish: Optional[str] = None
    last_ack: Optional[str] = None


mqtt_state = MQTTState()

# ── Price scraper state ────────────────────────────────────────────────────────
PRICE_CONFIG_FILE = BASE_DIR / "price_config.json"
_price_scraper: Optional[PriceScraper] = None
_scrape_task: Optional[asyncio.Task] = None


def load_price_config() -> dict:
    if PRICE_CONFIG_FILE.exists():
        with open(PRICE_CONFIG_FILE, encoding="utf-8") as f:
            return json.load(f)
    return {
        "source_types": ["bulk_retail", "lab_supplier"],
        "auto_refresh_on_startup": False,
        "delay_seconds": 1.5
    }


def save_price_config(cfg: dict):
    with open(PRICE_CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2)


def get_scraper() -> PriceScraper:
    global _price_scraper
    if _price_scraper is None:
        cfg = load_price_config()
        _price_scraper = PriceScraper(str(DB_FILE), cfg.get("source_types", ["bulk_retail"]))
    return _price_scraper


def load_mqtt_config() -> dict:
    if MQTT_CONFIG_FILE.exists():
        with open(MQTT_CONFIG_FILE, encoding="utf-8") as f:
            return json.load(f)
    return {
        "host": "localhost", "port": 1883,
        "topic_prefix": "nutribuddy",
        "client_id": "nutribuddy-server",
        "username": "", "password": "",
        "pump_map": {}
    }


def save_mqtt_config(cfg: dict):
    with open(MQTT_CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2)


def mqtt_connect():
    cfg = mqtt_state.config
    if not cfg.get("host"):
        return
    try:
        client = mqtt.Client(client_id=cfg.get("client_id", "nutribuddy"))
        if cfg.get("username"):
            client.username_pw_set(cfg["username"], cfg.get("password", ""))

        def on_connect(c, userdata, flags, rc):
            mqtt_state.connected = rc == 0
            log.info(f"MQTT {'connected' if rc == 0 else f'failed rc={rc}'}")
            if rc == 0:
                ack_topic = f"{cfg.get('topic_prefix', 'nutribuddy')}/ack/#"
                c.subscribe(ack_topic)

        def on_message(c, userdata, msg):
            mqtt_state.last_ack = f"{msg.topic}: {msg.payload.decode()}"
            log.info(f"MQTT ACK: {mqtt_state.last_ack}")
            asyncio.run_coroutine_threadsafe(
                broadcast({"type": "mqtt_ack", "topic": msg.topic,
                           "payload": msg.payload.decode()}),
                app_loop
            )

        def on_disconnect(c, userdata, rc):
            mqtt_state.connected = False
            log.warning(f"MQTT disconnected rc={rc}")

        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        client.connect_async(cfg["host"], cfg.get("port", 1883), keepalive=60)
        client.loop_start()
        mqtt_state.client = client
    except Exception as e:
        log.error(f"MQTT connect error: {e}")
        mqtt_state.connected = False


# ── WebSocket manager (same pattern as LoRaLink) ─────────────────────────────
ws_clients: list[WebSocket] = []
app_loop: asyncio.AbstractEventLoop = None


async def broadcast(msg: dict):
    dead = []
    for ws in ws_clients:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.append(ws)
    for ws in dead:
        ws_clients.remove(ws)


# ── App lifecycle ─────────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    global app_loop
    app_loop = asyncio.get_event_loop()
    init_db()
    get_scraper()   # initialise scraper + ensure prices table exists
    mqtt_state.config = load_mqtt_config()
    mqtt_connect()
    # Optional startup auto-refresh
    price_cfg = load_price_config()
    if price_cfg.get("auto_refresh_on_startup"):
        log.info("Auto price refresh on startup — launching background scrape…")
        asyncio.create_task(_background_scrape_all())
    log.info("NutriCalc server ready — http://localhost:8100")
    yield
    if mqtt_state.client:
        mqtt_state.client.loop_stop()
        mqtt_state.client.disconnect()


app = FastAPI(title="NutriCalc", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


# ── Pydantic models ───────────────────────────────────────────────────────────
class SolveRequest(BaseModel):
    targets: dict           # {element: ppm}
    compound_ids: list[int] # selected compound IDs
    volume_L: float = 100.0


class SaveFormulaRequest(BaseModel):
    name: str
    notes: str = ""
    solve_result: dict
    targets: dict
    compound_ids: list[int]
    volume_L: float


class CustomCompoundRequest(BaseModel):
    name: str
    formula: str
    purity: float = 99.0
    is_liquid: bool = False
    density: Optional[float] = None
    cost_per_kg: float = 0.0
    notes: str = ""
    elements: dict  # {element: pct}


class MQTTConfigRequest(BaseModel):
    host: str
    port: int = 1883
    topic_prefix: str = "nutribuddy"
    client_id: str = "nutribuddy-server"
    username: str = ""
    password: str = ""
    pump_map: dict = {}


class MQTTPublishRequest(BaseModel):
    formula_name: str
    volume_L: float
    channels: list[dict]   # [{pump, label, grams}]
    ec_target: float = 0.0
    ph_target: float = 6.0


class UpdateCompoundRequest(BaseModel):
    cost_per_kg: Optional[float] = None
    available: Optional[bool] = None
    notes: Optional[str] = None


# ── Routes ────────────────────────────────────────────────────────────────────

@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")


# ── Chemicals ─────────────────────────────────────────────────────────────────

@app.get("/api/chemicals")
async def get_chemicals():
    db = load_chemicals()
    all_compounds = get_all_compounds()
    return JSONResponse({
        "success": True,
        "elements": db["elements"],
        "element_labels": db["element_labels"],
        "compounds": all_compounds,
        "presets": db["presets"]
    })


@app.post("/api/chemicals")
async def add_custom_compound(req: CustomCompoundRequest):
    all_compounds = get_all_compounds()
    new_id = max((c["id"] for c in all_compounds), default=100) + 1
    compound = {
        "id": new_id,
        "name": req.name, "formula": req.formula,
        "purity": req.purity, "is_liquid": req.is_liquid,
        "density": req.density, "cost_per_kg": req.cost_per_kg,
        "available": True, "custom": True,
        "notes": req.notes, "elements": req.elements
    }
    with get_db() as db:
        db.execute("INSERT INTO custom_compounds (data) VALUES (?)", (json.dumps(compound),))
        db.commit()
    return JSONResponse({"success": True, "message": "Custom compound added.", "data": compound})


@app.patch("/api/chemicals/{compound_id}")
async def update_compound(compound_id: int, req: UpdateCompoundRequest):
    """Update mutable fields (cost, availability) on built-in or custom compounds."""
    db = load_chemicals()
    compounds = db["compounds"]

    # Validate cost_per_kg
    if req.cost_per_kg is not None:
        import math
        if math.isnan(req.cost_per_kg) or math.isinf(req.cost_per_kg):
            raise HTTPException(status_code=422, detail="Price must be a valid number.")
        if req.cost_per_kg < 0:
            raise HTTPException(status_code=422, detail="Price cannot be negative.")
        if req.cost_per_kg > 10000:
            raise HTTPException(status_code=422, detail="Price exceeds $10,000/kg — please verify.")

    # Check if it's a built-in compound
    for c in compounds:
        if c["id"] == compound_id:
            if req.cost_per_kg is not None:
                c["cost_per_kg"] = req.cost_per_kg
            if req.available is not None:
                c["available"] = req.available
            if req.notes is not None:
                c["notes"] = req.notes
            with open(CHEMICALS_FILE, "w") as f:
                json.dump(db, f, indent=2)
            return JSONResponse({"success": True, "message": "Compound updated.", "data": c})

    # Check custom compounds
    with get_db() as conn:
        row = conn.execute("SELECT rowid, data FROM custom_compounds").fetchall()
        for r in row:
            c = json.loads(r["data"])
            if c["id"] == compound_id:
                if req.cost_per_kg is not None:
                    c["cost_per_kg"] = req.cost_per_kg
                if req.available is not None:
                    c["available"] = req.available
                if req.notes is not None:
                    c["notes"] = req.notes
                conn.execute("UPDATE custom_compounds SET data=? WHERE rowid=?",
                             (json.dumps(c), r["rowid"]))
                conn.commit()
                return JSONResponse({"success": True, "message": "Compound updated.", "data": c})

    raise HTTPException(status_code=404, detail="Compound not found.")


# ── Solver ────────────────────────────────────────────────────────────────────

@app.post("/api/solve")
async def api_solve(req: SolveRequest):
    all_compounds = get_all_compounds()
    selected = [c for c in all_compounds if c["id"] in req.compound_ids]
    if not selected:
        raise HTTPException(status_code=400, detail="No valid compounds selected.")

    result = solve_formula(req.targets, selected, req.volume_L)

    # Build named weights for frontend convenience
    id_to_name = {c["id"]: c["name"] for c in selected}
    named_weights = {
        id_to_name[cid]: {"g_per_L": round(w, 4), "g_per_batch": round(w * req.volume_L, 2)}
        for cid, w in result.weights_g_per_L.items()
    }

    await broadcast({"type": "solve_complete", "ec": result.ec_estimated,
                     "cost": round(result.cost_per_batch, 3)})

    return JSONResponse({
        "success": result.success,
        "error": result.error,
        "weights_g_per_L": result.weights_g_per_L,
        "weights_g_per_batch": result.weights_g_per_batch,
        "named_weights": named_weights,
        "achieved_ppm": result.achieved_ppm,
        "target_ppm": result.target_ppm,
        "deviation_pct": result.deviation_pct,
        "residual_rms": round(result.residual_rms, 4),
        "ec_estimated": result.ec_estimated,
        "cost_per_batch": round(result.cost_per_batch, 4),
        "cost_complete": result.cost_complete,
        "volume_L": result.volume_L,
        "ab_split": result.ab_split,
        "warnings": result.warnings
    })


@app.post("/api/reverse_solve")
async def api_reverse_solve(body: dict):
    """Calculate PPM from manually entered weights (HydroBuddy Mode 2)."""
    weights = body.get("weights_g_per_L", {})
    compound_ids = list(weights.keys())
    all_compounds = get_all_compounds()
    selected = [c for c in all_compounds if str(c["id"]) in compound_ids or c["id"] in compound_ids]
    weights_int = {c["id"]: weights.get(c["id"], weights.get(str(c["id"]), 0)) for c in selected}
    ppm = reverse_solve(weights_int, selected)
    ec = estimate_ec(ppm)
    return JSONResponse({"success": True, "achieved_ppm": ppm, "ec_estimated": ec})


# ── Saved Formulas ────────────────────────────────────────────────────────────

@app.get("/api/formulas")
async def list_formulas():
    with get_db() as db:
        rows = db.execute("SELECT id, name, notes, created, modified FROM formulas ORDER BY modified DESC").fetchall()
    return JSONResponse({"success": True, "formulas": [dict(r) for r in rows]})


@app.post("/api/formulas")
async def save_formula(req: SaveFormulaRequest):
    now = datetime.utcnow().isoformat()
    payload = json.dumps({
        "solve_result": req.solve_result,
        "targets": req.targets,
        "compound_ids": req.compound_ids,
        "volume_L": req.volume_L
    })
    with get_db() as db:
        cur = db.execute(
            "INSERT INTO formulas (name, notes, created, modified, data) VALUES (?,?,?,?,?)",
            (req.name, req.notes, now, now, payload)
        )
        db.commit()
        formula_id = cur.lastrowid
    return JSONResponse({"success": True, "message": "Formula saved.", "data": {"id": formula_id}})


@app.get("/api/formulas/{formula_id}")
async def get_formula(formula_id: int):
    with get_db() as db:
        row = db.execute("SELECT * FROM formulas WHERE id=?", (formula_id,)).fetchone()
    if not row:
        raise HTTPException(status_code=404, detail="Formula not found.")
    result = dict(row)
    result["data"] = json.loads(result["data"])
    return JSONResponse({"success": True, "formula": result})


@app.delete("/api/formulas/{formula_id}")
async def delete_formula(formula_id: int):
    with get_db() as db:
        db.execute("DELETE FROM formulas WHERE id=?", (formula_id,))
        db.commit()
    return JSONResponse({"success": True, "message": "Formula deleted."})


# ── MQTT ──────────────────────────────────────────────────────────────────────

@app.get("/api/mqtt/config")
async def get_mqtt_config():
    cfg = {**mqtt_state.config}
    cfg.pop("password", None)  # never send password back to frontend
    return JSONResponse({"success": True, "config": cfg, "connected": mqtt_state.connected})


@app.put("/api/mqtt/config")
async def set_mqtt_config(req: MQTTConfigRequest):
    cfg = req.model_dump()
    mqtt_state.config = cfg
    save_mqtt_config(cfg)
    # Reconnect
    if mqtt_state.client:
        mqtt_state.client.loop_stop()
        mqtt_state.client.disconnect()
    mqtt_connect()
    return JSONResponse({"success": True, "message": "MQTT config saved. Reconnecting..."})


@app.post("/api/mqtt/publish")
async def mqtt_publish(req: MQTTPublishRequest):
    if not mqtt_state.connected or not mqtt_state.client:
        raise HTTPException(status_code=503, detail="MQTT not connected.")

    prefix = mqtt_state.config.get("topic_prefix", "nutribuddy")
    pump_map = mqtt_state.config.get("pump_map", {})
    timestamp = datetime.utcnow().isoformat() + "Z"

    # Full formula payload on summary topic
    payload = {
        "formula": req.formula_name,
        "volume_L": req.volume_L,
        "channels": req.channels,
        "ec_target": req.ec_target,
        "ph_target": req.ph_target,
        "timestamp": timestamp
    }
    summary_topic = f"{prefix}/dose"
    mqtt_state.client.publish(summary_topic, json.dumps(payload), qos=1)

    # Per-pump topics (e.g. nutribuddy/pump/1)
    published = []
    for ch in req.channels:
        pump_id = pump_map.get(ch.get("label"), ch.get("pump", 0))
        topic = f"{prefix}/pump/{pump_id}"
        pump_payload = {
            "pump": pump_id, "label": ch.get("label"),
            "grams": ch.get("grams"), "ml": ch.get("ml"),
            "formula": req.formula_name, "timestamp": timestamp
        }
        mqtt_state.client.publish(topic, json.dumps(pump_payload), qos=1)
        published.append({"topic": topic, "payload": pump_payload})

    mqtt_state.last_publish = timestamp
    log.info(f"MQTT published {len(published)} pump channels for '{req.formula_name}'")

    await broadcast({"type": "mqtt_publish", "channels": len(published), "timestamp": timestamp})

    return JSONResponse({
        "success": True,
        "message": f"Published to {len(published)} pump channels.",
        "data": {"summary_topic": summary_topic, "published": published}
    })


@app.get("/api/mqtt/status")
async def mqtt_status():
    return JSONResponse({
        "success": True,
        "connected": mqtt_state.connected,
        "last_publish": mqtt_state.last_publish,
        "last_ack": mqtt_state.last_ack,
        "config": {k: v for k, v in mqtt_state.config.items() if k != "password"}
    })


# ── WebSocket ─────────────────────────────────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_clients.append(ws)
    log.info(f"WebSocket client connected ({len(ws_clients)} total)")
    try:
        while True:
            data = await ws.receive_json()
            # Ping/pong keepalive
            if data.get("type") == "ping":
                await ws.send_json({"type": "pong"})
    except WebSocketDisconnect:
        if ws in ws_clients:
            ws_clients.remove(ws)
        log.info(f"WebSocket client disconnected ({len(ws_clients)} remaining)")


# ── Price endpoints ────────────────────────────────────────────────────────────

class PriceConfigRequest(BaseModel):
    source_types: list[str] = ["bulk_retail", "lab_supplier"]
    auto_refresh_on_startup: bool = False
    delay_seconds: float = 1.5


async def _background_scrape_all(compound_ids: list[int] = None):
    """Run scraper in thread pool so it doesn't block the event loop."""
    import concurrent.futures
    scraper = get_scraper()
    compounds = get_all_compounds()
    if compound_ids:
        compounds = [c for c in compounds if c["id"] in compound_ids]

    completed = 0
    total = len(compounds)

    def _progress(name, idx, tot):
        asyncio.run_coroutine_threadsafe(
            broadcast({"type": "scrape_progress", "compound": name, "done": idx, "total": tot}),
            app_loop
        )

    loop = asyncio.get_event_loop()
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        await loop.run_in_executor(pool, lambda: scraper.scrape_all(compounds, _progress))

    await broadcast({"type": "scrape_complete", "total": total})
    log.info(f"Background price scrape complete — {total} compounds.")


@app.get("/api/prices/config")
async def get_price_config():
    return JSONResponse({"success": True, "config": load_price_config()})


@app.put("/api/prices/config")
async def set_price_config(req: PriceConfigRequest):
    cfg = req.model_dump()
    save_price_config(cfg)
    # Reinitialise scraper with new source types
    global _price_scraper
    _price_scraper = PriceScraper(str(DB_FILE), cfg["source_types"])
    return JSONResponse({"success": True, "message": "Price config saved.", "data": cfg})


@app.get("/api/prices")
async def get_all_prices():
    """Return best known price for every compound that has been scraped."""
    scraper = get_scraper()
    best = scraper.get_all_best_prices()
    return JSONResponse({"success": True, "prices": best})


@app.get("/api/prices/{compound_id}")
async def get_compound_prices(compound_id: int):
    scraper = get_scraper()
    prices = scraper.get_prices(compound_id)
    best = scraper.get_best_price(compound_id)
    return JSONResponse({"success": True, "prices": prices, "best": best})


@app.post("/api/prices/refresh")
async def refresh_prices(body: dict = None):
    """
    Trigger a background price scrape.
    Body: {"compound_ids": [1,2,3]} to scrape specific compounds, or empty for all.
    """
    global _scrape_task
    if _scrape_task and not _scrape_task.done():
        return JSONResponse({"success": False, "message": "Scrape already in progress."}, status_code=409)

    compound_ids = (body or {}).get("compound_ids", None)
    _scrape_task = asyncio.create_task(_background_scrape_all(compound_ids))
    count = len(compound_ids) if compound_ids else len(get_all_compounds())
    await broadcast({"type": "scrape_started", "total": count})
    return JSONResponse({"success": True, "message": f"Scraping {count} compounds in background."})


@app.get("/api/prices/status")
async def scrape_status():
    running = bool(_scrape_task and not _scrape_task.done())
    return JSONResponse({"success": True, "running": running})


@app.delete("/api/prices/{compound_id}")
async def clear_compound_prices(compound_id: int):
    get_scraper().clear_prices(compound_id)
    return JSONResponse({"success": True, "message": "Prices cleared."})


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("server:app", host="0.0.0.0", port=8100, reload=True)
