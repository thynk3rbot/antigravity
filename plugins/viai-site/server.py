# plugins/viai-site/server.py
from fastapi import FastAPI, HTTPException, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import httpx
import os
import logging
from pathlib import Path

# Setup logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] viai-site: %(message)s")
log = logging.getLogger("viai-site")

app = FastAPI(title="viai.club Client Site")

# Serve static files
STATIC_DIR = Path(__file__).parent / "static"
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

# ── Health ──
@app.get("/health")
async def health():
    return {"status": "ok", "plugin": "viai-site", "port": 8010}

# ── API: Fleet Status ──
@app.get("/api/fleet-status")
async def fleet_status():
    """Return current fleet status for products UI."""
    # Priority 1: Live Daemon API
    daemon_url = os.getenv("DAEMON_URL", "http://localhost:8001/api/plugins/lvc-service")
    try:
        async with httpx.AsyncClient() as client:
            # Note: This is a placeholder for the real V3 LVC API
            # For now, we simulate the 'Magic-V3' fleet responses
            # resp = await client.get(daemon_url, timeout=2.0)
            # return resp.json()
            pass
    except Exception as e:
        log.warning(f"Daemon API unavailable: {e}")

    # Priority 2: High-quality demo data
    return {
        "devices": [
            {"id": "Magic-A3F2", "status": "Online", "battery": 87, "signal": -42},
            {"id": "Magic-B1E7", "status": "Online", "battery": 62, "signal": -58},
            {"id": "Magic-C4D9", "status": "Offline", "battery": 0, "signal": -120},
        ],
        "timestamp": "2026-04-01T12:00:00Z"
    }

# ── API: RAG Search (Dify) ──
@app.get("/api/rag/search")
async def rag_search(q: str):
    """Bridge to the Dify Knowledge Base."""
    dify_url = os.getenv("DIFY_BASE_URL", "http://localhost:8400/v1")
    dify_key = os.getenv("DIFY_API_KEY")

    if not dify_key:
        log.error("DIFY_API_KEY is not set.")
        return {"error": "Knowledge Base API key missing", "response": "AI Search is offline."}

    try:
        async with httpx.AsyncClient() as client:
            log.info(f"RAG Search: {q}")
            # Mocking the Dify chat completion for the demo site
            # In production: await client.post(...)
            return {
                "answer": f"Based on the Magic platform docs, '{q}' is typically managed via the NVS settings or the local setup tools.",
                "source": "viai.club Knowledge Base"
            }
    except Exception as e:
        log.error(f"Dify search error: {e}")
        return {"error": str(e)}

# ── Root ──
@app.get("/")
async def root():
    index_path = STATIC_DIR / "index.html"
    return FileResponse(str(index_path))

# Catch-all for SPA navigation or static routes
@app.get("/{path:path}")
async def catch_all(path: str):
    file_path = STATIC_DIR / path
    if file_path.exists() and file_path.is_file():
        return FileResponse(str(file_path))
    # Fallback to index for routing
    return FileResponse(str(STATIC_DIR / "index.html"))

if __name__ == "__main__":
    import uvicorn
    log.info("Starting viai.club site server on port 8010")
    uvicorn.run(app, host="0.0.0.0", port=8010)
