import sys
from pathlib import Path

# Add framework to path
FRAMEWORK_PATH = Path(__file__).parent.parent / "multi-agent-framework"
if str(FRAMEWORK_PATH) not in sys.path:
    sys.path.append(str(FRAMEWORK_PATH))

import json
import logging
import httpx
from fastapi import FastAPI, HTTPException, WebSocket
from fastapi.responses import JSONResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from typing import Optional
from session_manager import SessionManager
from domain_manager import DomainManager
from hybrid_model_proxy import HybridModelProxy
from rag.ingest import ingest
import socket
from zeroconf import ServiceBrowser, Zeroconf

class DiscoveryListener:
    def __init__(self):
        self.devices = []
    def add_service(self, zc, type, name):
        info = zc.get_service_info(type, name)
        if info:
            ip = socket.inet_ntoa(info.addresses[0])
            mac = info.properties.get(b'mac', b'Unknown').decode()
            hw = info.properties.get(b'hw', b'Unknown').decode()
            ver = info.properties.get(b'ver', b'Unknown').decode()
            self.devices.append({"name": name.split('.')[0], "ip": ip, "mac": mac, "hw": hw, "ver": ver})
    def remove_service(self, zc, type, name): pass
    def update_service(self, zc, type, name): pass

# Setup logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger("AssistantServer")

# Load config
CONFIG_PATH = Path(__file__).parent / "config.json"
with open(CONFIG_PATH, "r") as f:
    config = json.load(f)

# Initialize SessionManager
session_manager = SessionManager(Path(config["sessions"]["db_path"]))

# Initialize DomainManager
domain_manager = DomainManager(
    domains_dir=Path(__file__).parent / config["rag"]["domains_dir"],
    persist_dir=Path(__file__).parent / config["rag"]["persist_dir"],
    global_config=config
)

# Initialize HybridModelProxy
model_proxy = HybridModelProxy(config)

app = FastAPI(title=config["branding"]["app_name"])

# Mount static files
static_dir = Path(__file__).parent / "static"
if not static_dir.exists():
    static_dir.mkdir(parents=True, exist_ok=True)
app.mount("/static", StaticFiles(directory=str(static_dir)), name="static")

@app.get("/")
async def get_index():
    index_path = static_dir / "index.html"
    if index_path.exists():
        return FileResponse(index_path)
    return JSONResponse({"error": "UI not found. Please follow Step 2 to create the UI."}, status_code=404)

@app.get("/health")
async def health():
    """Health check endpoint. Validates Ollama availability."""
    ollama_url = config["model_proxy"]["local"]["base_url"]
    ollama_ok = False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(f"{ollama_url}/api/tags", timeout=2.0)
            if response.status_code == 200:
                ollama_ok = True
    except Exception as e:
        logger.warning(f"Ollama health check failed: {e}")

    return {
        "status": "ok",
        "ollama": ollama_ok,
        "app_name": config["branding"]["app_name"],
        "version": config["project"]["version"]
    }

@app.get("/api/branding")
async def get_branding():
    """Returns the branding configuration to the frontend."""
    return config.get("branding", {})

# ── Session Management Endpoints ─────────────────────────────────────

class CreateSessionRequest(BaseModel):
    title: str
    domain: Optional[str] = None
    session_id: Optional[str] = None

@app.get("/api/sessions")
async def list_sessions():
    """Returns a list of all chat sessions."""
    return {"sessions": session_manager.list_sessions()}

@app.post("/api/sessions")
async def create_session(req: CreateSessionRequest):
    """Creates a new chat session."""
    import uuid
    sid = req.session_id or str(uuid.uuid4())[:8]
    session_manager.create_session(sid, req.title, req.domain)
    return {"status": "ok", "session_id": sid}

@app.get("/api/sessions/{session_id}")
async def get_session_history(session_id: str):
    """Returns the message history for a specific session."""
    history = session_manager.get_history(session_id)
    return {"session_id": session_id, "messages": history}

@app.delete("/api/sessions/{session_id}")
async def delete_session(session_id: str):
    """Deletes a chat session."""
    session_manager.delete_session(session_id)
    return {"status": "ok"}

# ── Domain Management Endpoints ─────────────────────────────────────

@app.get("/api/domains")
async def list_domains():
    """Returns a list of all available RAG domains."""
    return {"domains": domain_manager.list_domains()}

class IngestRequest(BaseModel):
    domain: str

@app.post("/api/ingest")
async def trigger_ingestion(req: IngestRequest):
    """Triggers RAG ingestion for a specific domain library."""
    domain_cfg = domain_manager.get_config(req.domain)
    if not domain_cfg:
        raise HTTPException(status_code=404, detail="Domain not found")
        
    try:
        # Build ingestion config derived from global and domain settings
        ingest_config = config.copy()
        ingest_config["rag"] = {
            "persist_directory": str(domain_manager.persist_dir),
            "collection_name": domain_cfg.collection_name,
            "chunk_size": domain_cfg.chunk_size,
            "chunk_overlap": domain_cfg.chunk_overlap,
            "embedding_model": config.get("rag", {}).get("embedding_model", "nomic-embed-text")
        }
        
        # Run ingestion
        count = ingest(ingest_config, domain_cfg.sources)
        
        return {
            "status": "ok",
            "chunks_ingested": count,
            "collection": domain_cfg.collection_name
        }
    except Exception as e:
        logger.error(f"Ingestion failed for {req.domain}: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# ── Chat Implementation ─────────────────────────────────────────────

class ChatRequest(BaseModel):
    message: str
    session_id: str
    domain: Optional[str] = None

@app.post("/api/chat")
async def chat_endpoint(req: ChatRequest):
    """Core chat logic with RAG context injection."""
    # 1. Save user message to history
    session_manager.add_message(req.session_id, "user", req.message)
    
    # 2. Retrieve history for context (last 10 messages)
    history = session_manager.get_history(req.session_id, limit=10)
    history_str = "\n".join([f"{m['role']}: {m['content']}" for m in history[:-1]])
    
    # 3. Build prompt with domain context if available
    prompt = req.message
    system_prompt = "You are a helpful AI assistant."
    domain_ctx = ""
    
    if req.domain and req.domain != "general":
        domain_cfg = domain_manager.get_config(req.domain)
        if domain_cfg:
            system_prompt = domain_cfg.system_prompt
            # Query RAG
            rag_docs = await domain_manager.query(req.domain, req.message)
            if rag_docs:
                domain_ctx = "\n\nRelevant context from your library:\n" + \
                             "\n".join([f"- {d['text']} (Source: {d['source']})" for d in rag_docs])

    final_prompt = f"{system_prompt}\n\n{domain_ctx}\n\nRecent Chat History:\n{history_str}\n\nUser: {prompt}\nAssistant:"

    # 4. Query Model Proxy
    model = config["model_proxy"]["local"]["default_model"]
    result = await model_proxy.query(model=model, prompt=final_prompt)
    
    if result["success"]:
        bot_response = result["response"]
        session_manager.add_message(req.session_id, "bot", bot_response)
        return {
            "status": "ok",
            "response": bot_response,
            "session_id": req.session_id,
            "metrics": result.get("metrics", {})
        }
    else:
        raise HTTPException(status_code=500, detail=result.get("error", "AI Backend Error"))

@app.websocket("/ws/chat")
async def websocket_chat(websocket: WebSocket):
    """WebSocket endpoint for real-time chat streaming."""
    await websocket.accept()
    try:
        while True:
            data = await websocket.receive_json()
            if data.get("type") != "chat":
                continue
            
            message = data.get("message")
            session_id = data.get("session_id")
            domain = data.get("domain", "general")
            
            if not message or not session_id:
                await websocket.send_json({"type": "error", "content": "Missing message or session_id"})
                continue
            
            # 1. Save user message
            session_manager.add_message(session_id, "user", message)
            
            # 2. Build prompt (similar to Step 5)
            history = session_manager.get_history(session_id, limit=10)
            history_str = "\n".join([f"{m['role']}: {m['content']}" for m in history[:-1]])
            
            system_prompt = "You are a helpful AI assistant."
            domain_ctx = ""
            
            if domain and domain != "general":
                domain_cfg = domain_manager.get_config(domain)
                if domain_cfg:
                    system_prompt = domain_cfg.system_prompt
                    rag_docs = await domain_manager.query(domain, message)
                    if rag_docs:
                        domain_ctx = "\n\nContext:\n" + "\n".join([f"- {d['text']}" for d in rag_docs])

            final_prompt = f"{system_prompt}\n\n{domain_ctx}\n\nHistory:\n{history_str}\n\nUser: {message}\nAssistant:"
            
            # 3. Stream from proxy
            model = config["model_proxy"]["local"]["default_model"]
            full_response = ""
            
            async for token in model_proxy.stream(model=model, prompt=final_prompt):
                if isinstance(token, str):
                    full_response += token
                    await websocket.send_json({"type": "token", "content": token})
                elif isinstance(token, dict) and token.get("done"):
                    # Final metrics message
                    session_manager.add_message(session_id, "bot", full_response)
                    await websocket.send_json({
                        "type": "done", 
                        "session_id": session_id,
                        "metrics": token.get("metrics", {})
                    })
                elif isinstance(token, dict) and token.get("error"):
                    await websocket.send_json({"type": "error", "content": token["error"]})

    except Exception as e:
        logger.error(f"WebSocket error: {e}")
    finally:
        await websocket.close()

# ── Fleet Management Endpoints ─────────────────────────────────────

@app.get("/api/fleet/census")
async def fleet_census():
    """Performs a live mDNS discovery for Magic devices."""
    zc = Zeroconf()
    listener = DiscoveryListener()
    browser = ServiceBrowser(zc, "_http._tcp.local.", listener)
    
    # Wait for discovery (3 seconds is usually enough for local network)
    import asyncio
    await asyncio.sleep(3.0)
    zc.close()
    
    return {"status": "ok", "devices": listener.devices}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host=config["server"]["host"], port=config["server"]["port"])
