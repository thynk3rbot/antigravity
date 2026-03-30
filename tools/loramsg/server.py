"""
LMX Messenger Daemon — FastAPI server on port 8400.
Bridges USB serial to a WebSocket-served PWA chat UI.
"""

import asyncio
import json
import logging
import os
from pathlib import Path
from typing import List, Optional, Set

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

import store
import bridge as lmx_bridge

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger("LoraMsg")

app = FastAPI(title="LMX Messenger", version="0.1.0")

STATIC_DIR = Path(__file__).parent / "static"
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

# ── State ─────────────────────────────────────────────────────────────

_ws_clients: Set[WebSocket] = set()
_bridge: Optional[lmx_bridge.LMXSerialBridge] = None
_our_node_id: int = int(os.getenv("LMX_NODE_ID", "1"))
_serial_port: str = os.getenv("LMX_SERIAL_PORT", "COM3")

# ── WebSocket broadcast ───────────────────────────────────────────────

async def _broadcast(event: dict):
    dead = set()
    for ws in _ws_clients:
        try:
            await ws.send_json(event)
        except Exception:
            dead.add(ws)
    _ws_clients.difference_update(dead)

# ── LMX event callbacks ───────────────────────────────────────────────

async def _on_message(src: int, dest: int, packet_id: int, text: str, hops_used: int):
    msg_id = store.save(packet_id, src, dest, "rx", text, hops_used)
    logger.info(f"RX #{packet_id} from 0x{src:02X}: {text}")
    await _broadcast({
        "type":      "rx",
        "id":        msg_id,
        "packet_id": packet_id,
        "src":       src,
        "dest":      dest,
        "text":      text,
        "hops_used": hops_used,
    })

async def _on_ack(packet_id: int):
    store.ack(packet_id)
    logger.info(f"ACK for pkt #{packet_id}")
    await _broadcast({"type": "ack", "packet_id": packet_id})

# ── Startup / Shutdown ────────────────────────────────────────────────

@app.on_event("startup")
async def startup():
    store.init()
    global _bridge
    _bridge = lmx_bridge.LMXSerialBridge(
        port=_serial_port,
        on_message=_on_message,
        on_ack=_on_ack
    )
    await _bridge.start()
    logger.info(f"LMX Messenger ready on :8400 (node=0x{_our_node_id:02X}, serial={_serial_port})")

@app.on_event("shutdown")
async def shutdown():
    if _bridge:
        _bridge.stop()

# ── REST API ──────────────────────────────────────────────────────────

@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")

@app.get("/health")
async def health():
    return {"status": "ok", "node_id": _our_node_id, "serial": _serial_port}

@app.get("/api/messages")
async def get_messages(limit: int = 100):
    return {"messages": store.recent(limit)}

class SendRequest(BaseModel):
    dest: int
    text: str

@app.post("/api/send")
async def send_message(req: SendRequest):
    if not _bridge:
        return JSONResponse({"error": "Bridge not connected"}, status_code=503)
    await _bridge.send_text(req.dest, req.text)
    msg_id = store.save(0, _our_node_id, req.dest, "tx", req.text)
    event = {
        "type":  "tx",
        "id":    msg_id,
        "src":   _our_node_id,
        "dest":  req.dest,
        "text":  req.text,
        "status": "sent",
    }
    await _broadcast(event)
    return {"ok": True, "id": msg_id}

# ── WebSocket ─────────────────────────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    _ws_clients.add(ws)
    # Send message history on connect
    await ws.send_json({"type": "history", "messages": store.recent(50)})
    try:
        while True:
            data = await ws.receive_json()
            if data.get("type") == "send":
                req = SendRequest(dest=data["dest"], text=data["text"])
                await send_message(req)
    except WebSocketDisconnect:
        pass
    finally:
        _ws_clients.discard(ws)

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8400, log_level="info")
