import os
import logging
import json
import re
from typing import Optional
from fastapi import FastAPI, Request, Form
from fastapi.responses import PlainTextResponse, FileResponse
from fastapi.staticfiles import StaticFiles
import httpx
from pydantic import BaseModel
from twilio.twiml.messaging_response import MessagingResponse
import asyncio
import threading

# Configuration
ANTIGRAVITY_API_URL = os.getenv("ANTIGRAVITY_API_URL", "http://localhost:8000/api")
RAG_ROUTER_URL = os.getenv("RAG_ROUTER_URL", "http://localhost:8200/api")
MAGIC_PORT = int(os.getenv("MAGIC_PORT", "8500"))
SERIAL_PORT = os.getenv("MAGIC_SERIAL_PORT", "COM3") # Set to None to disable
BAUD_RATE = int(os.getenv("MAGIC_BAUD", "115200"))

# Logger
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("magic_bridge")

app = FastAPI(title="Magic Bridge")

# Mount Static Files for PWA
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
os.makedirs(STATIC_DIR, exist_ok=True)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

class MsgRequest(BaseModel):
    message: str
    user: str

@app.get("/")
async def index():
    return FileResponse(os.path.join(STATIC_DIR, "index.html"))

@app.get("/api/health")
async def health():
    return {"status": "magic is in the air", "bridge": "active"}

@app.post("/api/msg")
async def pwa_message(req: MsgRequest):
    """
    Handle message from the 'Magic' PWA Chat Interface.
    """
    logger.info(f"Magic PWA Message from {req.user}: {req.message}")
    response_text = await process_magic_command(req.message, req.user)
    return {"response": response_text}

@app.post("/webhook/twilio", response_class=PlainTextResponse)
async def twilio_webhook(Body: str = Form(...), From: str = Form(...)):
    """
    Handle incoming WhatsApp/SMS from Twilio.
    """
    logger.info(f"Magic Message from {From}: {Body}")
    
    # Process Magic Commands
    response_text = await process_magic_command(Body, From)
    
    # Twilio Response
    twiml = MessagingResponse()
    twiml.message(response_text)
    return str(twiml)

async def process_magic_command(text: str, user_id: str) -> str:
    """
    The 'Magic' brain. Translates natural language to system commands.
    """
    text = text.strip().lower()
    
    # 1. Check for 'Magic' prefix or just handle directly if it's a dedicated number
    if text.startswith("magic"):
        text = text.replace("magic", "", 1).strip().strip(",").strip()

    # 2. Simple Command Router (MVP)
    if "status" in text:
        return await get_system_status()
    
    if text.startswith("ask"):
        query = text.replace("ask", "", 1).strip()
        return await query_ai(query, user_id)

    if "on" in text or "activate" in text:
        pin = extract_pin(text)
        if pin:
            return await send_command(f"GPIO {pin} 1")
    
    if "off" in text or "deactivate" in text:
        pin = extract_pin(text)
        if pin:
            return await send_command(f"GPIO {pin} 0")

    # 3. Fallback to help
    return "Magic received your request, but I'm not sure how to help yet. Try 'Magic, status' or 'Magic, pin 5 on'."

async def get_system_status() -> str:
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(f"{ANTIGRAVITY_API_URL}/status")
            if resp.status_code == 200:
                data = resp.json()
                active_node = data.get("node", "Unknown")
                uptime = data.get("uptime", "N/A")
                return f"✨ Magic System Status:\nNode: {active_node}\nUptime: {uptime}s\nAll systems operational."
    except Exception as e:
        logger.error(f"Failed to fetch status: {e}")
    return "⚠️ Magic is having trouble reaching the main system. Check your connection."

async def send_command(cmd: str) -> str:
    try:
        logger.info(f"Executing Magic Command: {cmd}")
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.post(f"{ANTIGRAVITY_API_URL}/cmd", json={"cmd": cmd, "node_id": None})
            if resp.status_code == 200:
                return f"✅ Magic executed: {cmd}"
    except Exception as e:
        logger.error(f"Command execution failed: {e}")
    return f"❌ Magic failed to execute: {cmd}"

async def query_ai(prompt: str, user_id: str) -> str:
    try:
        logger.info(f"Magic AI Query: {prompt}")
        async with httpx.AsyncClient(timeout=30.0) as client:
            # We use the RAG Router's manual query endpoint
            # In a real setup, we might dynamically determine the node_id
            resp = await client.post(f"{RAG_ROUTER_URL}/query", json={
                "node_id": "MAGIC_USER",
                "query": prompt
            })
            if resp.status_code == 200:
                data = resp.json()
                return data.get("answer", "Magic couldn't find an answer.")
    except Exception as e:
        logger.error(f"AI Query failed: {e}")
    return "⚠️ Magic is having trouble reaching the AI brain. Is the RAG Router running?"

def serial_listener():
    """Reads Serial for AI_QUERY: payloads and routes them."""
    if not SERIAL_PORT:
        return
    
    import serial
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        logger.info(f"Magic Serial Listener active on {SERIAL_PORT}")
        
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if "AI_QUERY:" in line:
                    prompt = line.split("AI_QUERY:", 1)[1].strip()
                    logger.info(f"Magic Serial AI Query: {prompt}")
                    
                    # Call RAG Router (Synchronously since we are in a thread)
                    try:
                        resp = httpx.post(f"{RAG_ROUTER_URL}/query", json={
                            "node_id": "MESH_NODE",
                            "query": prompt
                        }, timeout=30.0)
                        if resp.status_code == 200:
                            ans = resp.json().get("answer", "No answer.")
                            # Send back to mesh
                            ser.write(f"ALL AI: {ans}\n".encode('utf-8'))
                    except Exception as e:
                         logger.error(f"Serial AI forward failed: {e}")
            time.sleep(0.1)
    except Exception as e:
        logger.error(f"Serial listener failed: {e}")

def extract_pin(text: str) -> Optional[int]:
    match = re.search(r"pin\s*(\d+)", text)
    if match:
        return int(match.group(1))
    return None

if __name__ == "__main__":
    import uvicorn
    import time
    
    # Start Serial Listener in a background thread
    if SERIAL_PORT and SERIAL_PORT.lower() != "none":
        threading.Thread(target=serial_listener, daemon=True).start()
        
    logger.info(f"Magic starting on port {MAGIC_PORT}")
    uvicorn.run(app, host="0.0.0.0", port=MAGIC_PORT)
