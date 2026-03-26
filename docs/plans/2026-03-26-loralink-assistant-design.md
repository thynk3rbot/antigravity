# LoRaLink Assistant — Production Specification
### An Orion Framework Instance

**Date:** 2026-03-26
**Status:** Design approved, ready for AG implementation
**Authors:** Claude + User (brainstorming), AG (implementation)
**Target:** Standalone Windows app, evolving into cross-platform AI assistant
**Framework:** Orion (tools/multi-agent-framework/)
**Current Branding:** LoRaLink(tm) Copyright 2026 spw1.com. All Rights Reserved.

> **IMPERATIVE — BRANDING IS CONFIG-DRIVEN:** All user-facing brand strings (app name, tagline, copyright, logo, accent color) MUST be read from `config.json` `"branding"` section at runtime. NEVER hardcode "LoRaLink", logo paths, or color values in HTML/CSS/JS/Python. The branding will change in the future. Every UI element, window title, tray tooltip, page header, and footer must pull from config. This is a hard requirement.

## Problem

The user has multiple local knowledge domains (garden, firmware, tax) stored in PDFs, markdown, and code files. They want a single, private, local-first AI assistant that:
1. Queries any domain via natural language using Ollama + RAG
2. Runs as a persistent Windows system tray app with a browser UI
3. Evolves toward LoRaLink device communication and mobile apps

Everything runs locally. No data leaves the machine unless Ollama is unavailable and the user explicitly enables cloud fallback.

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│  System Tray (pystray)                              │
│  - Health dots: Ollama (green/red), Server (green/red)│
│  - Left-click: open browser UI                       │
│  - Right-click: menu (Open UI, Settings, Quit)       │
└────────────────────┬────────────────────────────────┘
                     │ launches
┌────────────────────▼────────────────────────────────┐
│  FastAPI Server (localhost:8300)                     │
│                                                     │
│  GET  /              → Chat UI (static HTML/JS/CSS) │
│  GET  /health        → {"status":"ok","ollama":bool}│
│  GET  /api/domains   → list available RAG domains   │
│  POST /api/chat      → query with streaming response│
│  WS   /ws/chat       → WebSocket streaming          │
│  POST /api/ingest    → trigger document ingestion   │
│  GET  /api/stats     → token/cost/latency metrics   │
│                                                     │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │ RAG Manager │  │ Hybrid Proxy │  │ Session Mgr│ │
│  │ (ChromaDB)  │  │ (Ollama/Cloud│  │ (SQLite)   │ │
│  └──────┬──────┘  └──────┬───────┘  └─────┬──────┘ │
│         │                │                │         │
│    per-domain        routes to         chat history  │
│    collections       local/cloud       per session   │
└─────────────────────────────────────────────────────┘
```

## Branding Architecture

**ALL branding is config-driven. This is non-negotiable.**

```json
{
    "branding": {
        "app_name": "LoRaLink Assistant",
        "tagline": "Your Local AI Knowledge Assistant",
        "copyright": "LoRaLink(tm) Copyright 2026 spw1.com. All Rights Reserved.",
        "icon_path": "static/media/loralink_icon.png",
        "accent_color": "#00b4d8",
        "theme": "dark"
    }
}
```

Every place brand identity appears in code must reference `config["branding"]`:

| Location | What reads from config |
|----------|----------------------|
| System tray tooltip | `branding.app_name` |
| Browser tab title | `branding.app_name` |
| Chat UI header | `branding.app_name` + `branding.tagline` |
| Page footer | `branding.copyright` |
| Tray icon | `branding.icon_path` |
| CSS accent color | `branding.accent_color` |
| FastAPI title | `branding.app_name` |
| Log prefix | `branding.app_name` |

**The `/api/branding` endpoint** serves branding config to the frontend:
```python
@app.get("/api/branding")
async def get_branding():
    return config.get("branding", {})
```

The frontend loads branding on startup and applies it to all UI elements. CSS custom properties are set dynamically from the config accent color.

**To rebrand:** Change `config.json` `"branding"` section. Restart the server. Done.

---

## Component Specifications

### 1. System Tray (`tray.py`)

**Base pattern:** Copy from `tools/daemon/tray.py` (190 lines, proven on Windows).

```python
# Key differences from daemon tray.py:
ASSISTANT_URL = "http://localhost:8300"
OLLAMA_URL = "http://localhost:11434"

# Health dots:
#   Top-right: Ollama status (green = running, red = down)
#   Bottom-right: Assistant server (green = running, yellow = starting)

# Menu:
#   "Open Assistant"     → webbrowser.open(ASSISTANT_URL)  [default/left-click]
#   "Settings"           → webbrowser.open(ASSISTANT_URL + "/settings")
#   ---
#   "Restart Server"     → kill + relaunch server subprocess
#   ---
#   "Quit"               → stop server, exit tray

# Icon: Reuse LoRaLink brand icon from:
#   tools/webapp/static/media/loralink_icon.png
```

**Startup sequence:**
1. Tray starts, shows yellow dots
2. Launches FastAPI server as subprocess
3. Polls `/health` every 5 seconds, updates dots
4. On first successful health check, auto-opens browser to `/`

### 2. FastAPI Server (`server.py`)

**Port:** 8300 (avoids conflict with webapp:8000 and daemon:8001)

```python
from fastapi import FastAPI, WebSocket
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import uvicorn

app = FastAPI(title="LoRaLink Assistant")
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def index():
    return FileResponse("static/index.html")

@app.get("/health")
async def health():
    ollama_ok = await check_ollama()
    return {
        "status": "ok",
        "ollama": ollama_ok,
        "domains": list(domain_manager.list_domains()),
        "model": config["model_proxy"]["local"]["default_model"]
    }
```

### 3. Domain Manager (`domain_manager.py`)

Manages multiple RAG knowledge bases. Each domain is a separate ChromaDB collection.

```python
class DomainManager:
    """
    Scans a domains directory for config files.
    Each domain has: name, description, sources[], collection_name, chunk_size.
    """

    def __init__(self, domains_dir: Path, persist_dir: Path):
        self.domains_dir = domains_dir
        self.persist_dir = persist_dir
        self._domains: dict[str, DomainConfig] = {}
        self._load_domains()

    def list_domains(self) -> list[dict]:
        """Return all available domains with doc counts."""
        return [
            {
                "id": d.id,
                "name": d.name,
                "description": d.description,
                "doc_count": self._get_doc_count(d.id),
                "sources": d.sources,
            }
            for d in self._domains.values()
        ]

    async def query(self, domain_id: str, question: str, top_k: int = 8) -> list[dict]:
        """Retrieve relevant chunks from a specific domain."""
        # Uses the framework's rag/retriever.py internally
        ...

    async def ingest(self, domain_id: str) -> int:
        """Re-ingest all sources for a domain. Returns chunk count."""
        ...
```

**Domain config format** (`domains/garden.json`):
```json
{
    "id": "garden",
    "name": "Garden Knowledge",
    "description": "PDFs, books, and notes about gardening, soil science, and botany",
    "sources": ["C:/Users/spw1/Documents/Garden/pdfs/", "C:/Users/spw1/Documents/Garden/books/"],
    "collection_name": "garden_knowledge",
    "chunk_size": 1024,
    "chunk_overlap": 100,
    "top_k": 8,
    "system_prompt": "You are a knowledgeable garden expert. Answer using ONLY the provided context from the user's garden reference library. Be practical and specific — include timing, quantities, and conditions. Cite source documents."
}
```

**Built-in domains to ship:**
- `garden.json` — Garden knowledge (user's PDFs)
- `firmware.json` — LoRaLink firmware docs and code
- Users can add more by dropping a JSON file into `domains/`

### 4. Chat API (`routes/chat.py`)

**POST /api/chat** — Single-response mode:
```python
@router.post("/api/chat")
async def chat(request: ChatRequest):
    """
    Request:  { "message": str, "domain": str|null, "session_id": str|null }
    Response: { "response": str, "domain": str, "sources": [...], "metrics": {...} }
    """
    # 1. Auto-detect domain if not specified (keyword matching)
    # 2. Retrieve RAG context from domain's collection
    # 3. Build prompt with system instruction + context + chat history
    # 4. Send to Ollama via hybrid proxy
    # 5. Save to session history
    # 6. Return response with sources and metrics
```

**WS /ws/chat** — Streaming mode (primary):
```python
@router.websocket("/ws/chat")
async def ws_chat(websocket: WebSocket):
    await websocket.accept()
    while True:
        data = await websocket.receive_json()
        # data: { "message": str, "domain": str|null, "session_id": str|null }

        # Stream tokens as they arrive from Ollama:
        async for token in proxy.stream(model, prompt):
            await websocket.send_json({
                "type": "token",
                "content": token
            })

        # Final message with metadata:
        await websocket.send_json({
            "type": "done",
            "sources": [...],
            "metrics": {"tokens": N, "latency_ms": M, "backend": "ollama"}
        })
```

**Ollama streaming:** Use `/api/generate` with `stream: true`. The response is newline-delimited JSON — each line has `{"response": "token", "done": false}` until `{"done": true, ...metrics}`.

### 5. Session Manager (`session_manager.py`)

SQLite-backed chat history. Lightweight, no ORM.

```python
# Schema:
# sessions(id TEXT PK, domain TEXT, created_at TEXT, last_active TEXT, title TEXT)
# messages(id INTEGER PK, session_id TEXT FK, role TEXT, content TEXT, created_at TEXT)

class SessionManager:
    def __init__(self, db_path: Path):
        self.db_path = db_path
        self._init_db()

    def create_session(self, domain: str = None) -> str: ...
    def add_message(self, session_id: str, role: str, content: str): ...
    def get_history(self, session_id: str, limit: int = 20) -> list[dict]: ...
    def list_sessions(self, limit: int = 50) -> list[dict]: ...
    def delete_session(self, session_id: str): ...
```

**Context window management:** Include last 10 messages (5 turns) in prompt. Older messages summarized on demand.

### 6. Chat UI (`static/index.html`)

Single-page app. Dark theme matching LoRaLink branding.

**Layout:**
```
┌──────────────────────────────────────────────────┐
│  LoRaLink Assistant          [Garden ▾] [⚙️]     │  ← Header: domain selector, settings
├──────────┬───────────────────────────────────────┤
│ Sessions │  Chat Area                            │
│          │                                       │
│ Today    │  🤖 Welcome! Ask me about your garden │
│ > tomato │     knowledge base.                   │
│ > soil   │                                       │
│          │  You: when to start tomato seeds?     │
│ Yesterday│                                       │
│ > pH     │  🤖 Based on your reference library..│
│          │     [streaming tokens appear here]    │
│          │                                       │
│          │  📚 Sources: seed_starting.pdf (p.12) │
│          │                                       │
├──────────┴───────────────────────────────────────┤
│  [🎤]  Type your question...            [Send ➤] │  ← Input bar with mic button
└──────────────────────────────────────────────────┘
```

**Design tokens** (defaults — accent color overridden by `config.branding.accent_color`):
```css
:root {
    --bg-primary: #0a0e17;       /* Deep navy */
    --bg-secondary: #111827;     /* Card background */
    --bg-input: #1a2233;         /* Input fields */
    --text-primary: #e2e8f0;     /* Light text */
    --text-secondary: #94a3b8;   /* Muted text */
    --accent: #00b4d8;           /* DEFAULT — overridden by config.branding.accent_color */
    --accent-hover: #0096b7;
    --success: #00d464;          /* Green status */
    --error: #dc3232;            /* Red status */
    --border: #1e293b;
    --font-mono: 'JetBrains Mono', 'Fira Code', monospace;
    --font-sans: 'Inter', system-ui, sans-serif;
}
```

**On page load, JS must call `/api/branding` and apply:**
```javascript
fetch('/api/branding').then(r => r.json()).then(b => {
    document.title = b.app_name || 'Assistant';
    document.querySelector('.app-title').textContent = b.app_name || '';
    document.querySelector('.app-tagline').textContent = b.tagline || '';
    document.querySelector('.copyright').textContent = b.copyright || '';
    if (b.accent_color) document.documentElement.style.setProperty('--accent', b.accent_color);
});
```

**Key UI behaviors:**
- Messages stream token-by-token via WebSocket (typing effect)
- Domain selector dropdown updates available domains from `/api/domains`
- Session sidebar lists past conversations, click to restore
- Source citations shown as collapsible pills below each AI response
- Status bar at bottom shows: backend (ollama/cloud), tokens, latency, cost
- Mobile-responsive: sidebar collapses to hamburger on narrow screens

### 7. Voice Input (`static/js/voice.js`)

Uses Web Speech API (browser-native, no Python dependencies).

```javascript
class VoiceInput {
    constructor(chatInput, sendButton) {
        this.recognition = new (window.SpeechRecognition ||
                                window.webkitSpeechRecognition)();
        this.recognition.continuous = false;
        this.recognition.interimResults = true;
        this.recognition.lang = 'en-US';
        this.isListening = false;
    }

    // Push-to-talk: hold mic button to record, release to send
    startListening() {
        this.recognition.start();
        this.isListening = true;
        // Show waveform indicator on mic button
    }

    stopListening() {
        this.recognition.stop();
        this.isListening = false;
    }

    // On result: populate chat input, auto-send on final result
    onResult(event) {
        const transcript = event.results[0][0].transcript;
        this.chatInput.value = transcript;
        if (event.results[0].isFinal) {
            this.sendButton.click();  // Auto-send on release
        }
    }
}
```

**Mic button states:**
- Idle: outline mic icon
- Listening: pulsing red mic icon with waveform
- Processing: spinner

### 8. Hybrid Model Proxy Integration

Reuse the framework's `hybrid_model_proxy.py` directly. Config:

```json
{
    "model_proxy": {
        "local": {
            "provider": "ollama",
            "base_url": "http://localhost:11434",
            "default_model": "qwen2.5-coder:14b"
        },
        "cloud": {
            "provider": "openrouter",
            "base_url": "https://openrouter.ai/api/v1",
            "api_key_env": "OPENROUTER_API_KEY",
            "default_model": "anthropic/claude-3-sonnet"
        },
        "prefer_local": true
    }
}
```

**Streaming addition to proxy:** Add `async def stream()` method:
```python
async def stream(self, model: str, prompt: str, **kwargs):
    """Yield tokens as they arrive from Ollama."""
    async with httpx.AsyncClient() as client:
        async with client.stream(
            "POST",
            f"{self.ollama_url}/api/generate",
            json={"model": model, "prompt": prompt, "stream": True},
            timeout=120.0
        ) as response:
            async for line in response.aiter_lines():
                if line:
                    data = json.loads(line)
                    if not data.get("done"):
                        yield data.get("response", "")
                    else:
                        # Final message — yield metrics
                        yield {"done": True, "metrics": data}
```

## Directory Structure

```
tools/assistant/
├── main.py                   # Entry point: launches tray + server
├── server.py                 # FastAPI app, routes, WebSocket
├── tray.py                   # System tray (pystray) — based on daemon/tray.py
├── domain_manager.py         # Multi-domain RAG management
├── session_manager.py        # SQLite chat history
├── config.json               # App configuration
├── domains/                  # Domain configs (one JSON per knowledge base)
│   ├── garden.json
│   └── firmware.json
├── static/                   # Frontend assets
│   ├── index.html            # Single-page chat UI
│   ├── css/
│   │   └── style.css         # Dark theme matching LoRaLink
│   ├── js/
│   │   ├── app.js            # Chat logic, WebSocket, session management
│   │   ├── voice.js          # Web Speech API push-to-talk
│   │   └── domains.js        # Domain selector logic
│   └── media/
│       └── (symlink or copy loralink_icon.png)
├── data/                     # Runtime data (gitignored)
│   ├── sessions.db           # SQLite chat history
│   └── chromadb/             # Vector store per domain
└── requirements.txt          # fastapi, uvicorn, httpx, chromadb, pystray, Pillow
```

## API Contracts

### POST /api/chat

```
Request:
{
    "message": "when should I start tomato seeds indoors?",
    "domain": "garden",          // null = auto-detect
    "session_id": "abc123",      // null = new session
    "stream": false              // true = use WebSocket instead
}

Response:
{
    "response": "Based on your Seed Starting Guide (p.12)...",
    "session_id": "abc123",
    "domain": "garden",
    "sources": [
        {"file": "seed_starting.pdf", "page": 12, "score": 0.87, "excerpt": "..."},
        {"file": "vegetable_gardening.pdf", "page": 45, "score": 0.72, "excerpt": "..."}
    ],
    "metrics": {
        "backend": "ollama",
        "model": "qwen2.5-coder:14b",
        "tokens": 342,
        "latency_ms": 8500,
        "cost": 0.00,
        "rag_results": 5
    }
}
```

### WS /ws/chat

```
Client sends:
{ "type": "chat", "message": "...", "domain": "garden", "session_id": "abc123" }

Server sends (sequence):
{ "type": "token", "content": "Based " }
{ "type": "token", "content": "on " }
{ "type": "token", "content": "your " }
...
{ "type": "done", "session_id": "abc123", "sources": [...], "metrics": {...} }
```

### GET /api/domains

```
Response:
{
    "domains": [
        {"id": "garden", "name": "Garden Knowledge", "doc_count": 667, "description": "..."},
        {"id": "firmware", "name": "LoRaLink Firmware", "doc_count": 1357, "description": "..."}
    ],
    "active": "garden"
}
```

### POST /api/ingest

```
Request:  { "domain": "garden" }
Response: { "status": "ok", "chunks_added": 245, "total_chunks": 912 }
```

### GET /api/sessions

```
Response:
{
    "sessions": [
        {"id": "abc123", "title": "Tomato seed starting", "domain": "garden",
         "last_active": "2026-03-26T14:30:00", "message_count": 6},
        ...
    ]
}
```

## Production Requirements

### Error Handling

| Scenario | Behavior |
|----------|----------|
| Ollama not running | Show red dot in tray. UI shows banner: "Ollama is offline. Start it or enable cloud fallback." Chat still works if cloud fallback enabled. |
| Ollama model not pulled | Detect via `/api/tags`. Show: "Model X not found. Run: `ollama pull X`" |
| ChromaDB empty (no docs ingested) | Answer without RAG context. Show: "No documents ingested for this domain. Answers may be less accurate." |
| WebSocket disconnect | Auto-reconnect with exponential backoff (1s, 2s, 4s, max 30s). Show reconnecting indicator. |
| Cloud API key missing | Disable cloud fallback silently. Only show error if user explicitly requests cloud. |
| Large PDF ingestion | Show progress in UI. Use background task. Don't block the chat. |

### Security

- **No auth required** — localhost only, not exposed to network
- Bind FastAPI to `127.0.0.1` only (never `0.0.0.0`)
- Sanitize all user input before injecting into prompts (prevent prompt injection from document content)
- Never log full prompts or responses to disk (privacy)
- `.gitignore` the `data/` directory (sessions, vector store)

### Logging

```python
import logging

# File: data/assistant.log (rotating, 5MB max, 3 backups)
# Console: INFO level
# File: DEBUG level

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
```

Log: startup/shutdown, domain switches, query latency, errors. Never log message content.

### Graceful Shutdown

```python
# In main.py:
async def shutdown():
    # 1. Stop accepting new WebSocket connections
    # 2. Wait for active streams to finish (max 5s timeout)
    # 3. Close ChromaDB connections
    # 4. Close SQLite connection
    # 5. Stop uvicorn server
    # 6. Exit tray
```

Tray quit and Ctrl+C both trigger the same shutdown sequence.

### Testing

| Test | What it verifies |
|------|-----------------|
| `test_health.py` | Server starts, `/health` returns 200, Ollama status correct |
| `test_domains.py` | Domain loading, listing, adding new domain JSON |
| `test_session.py` | Create/list/delete sessions, message persistence |
| `test_chat.py` | End-to-end: message → RAG → proxy → response (mock Ollama) |
| `test_stream.py` | WebSocket streaming: tokens arrive in order, done message has metrics |
| `test_voice.py` | N/A (browser-only, manual test) |
| `test_ingest.py` | Ingest PDFs/MD, verify chunks in ChromaDB, query returns results |

Use `pytest` + `httpx.AsyncClient` for API tests. Mock Ollama with a simple FastAPI fake.

## Dependencies

```
# requirements.txt
fastapi>=0.104.0
uvicorn[standard]>=0.24.0
httpx>=0.25.0
chromadb>=1.5.0
pystray>=0.19.0
Pillow>=10.0.0
aiofiles>=23.0.0
PyMuPDF>=1.23.0     # PDF text extraction (optional but recommended)
```

## Implementation Order

AG should implement in this order. Each step is independently testable.

| Step | Files | Description | Test |
|------|-------|-------------|------|
| 1 | `server.py`, `config.json` | Bare FastAPI server with `/health` endpoint | `curl localhost:8300/health` |
| 2 | `static/index.html`, `static/css/style.css` | Chat UI shell (dark theme, layout, no logic) | Open in browser |
| 3 | `session_manager.py` | SQLite session CRUD | `test_session.py` |
| 4 | `domain_manager.py`, `domains/garden.json` | Domain loading + RAG retrieval | `test_domains.py` |
| 5 | `routes/chat.py` (POST) | Non-streaming chat endpoint | `curl -X POST /api/chat` |
| 6 | `static/js/app.js` | Wire UI to POST endpoint, render responses | Manual browser test |
| 7 | `routes/chat.py` (WS) | WebSocket streaming | `test_stream.py` |
| 8 | `static/js/app.js` | Switch UI to WebSocket, token-by-token rendering | Manual browser test |
| 9 | `static/js/voice.js` | Push-to-talk mic button | Manual browser test |
| 10 | `static/js/domains.js` | Domain selector dropdown, ingest trigger | Manual browser test |
| 11 | `tray.py` | System tray with health dots | Run on Windows |
| 12 | `main.py` | Entry point: tray launches server | `python main.py` |
| 13 | Error handling | All error scenarios from table above | `test_chat.py` edge cases |
| 14 | Polish | Logging, graceful shutdown, session sidebar | Full integration test |

## Future Evolution (not in scope, but design for it)

These are NOT to be implemented now, but the architecture should not prevent them:

1. **LoRaLink device comms** — WebSocket bridge from assistant to daemon (port 8001) for sending commands to mesh devices. The chat UI would gain a "Devices" panel.
2. **Wake word** — Replace push-to-talk with always-listening wake word ("Hey LoRa"). Requires `vosk` or `pvporcupine` Python package.
3. **TTS responses** — Stream audio back via Web Audio API. Requires `edge-tts` or `piper` Python package.
4. **Android/iOS app** — The WebSocket API is already mobile-ready. Wrap in a WebView or build native UI consuming the same endpoints.
5. **Windows installer** — PyInstaller or NSIS to create `.exe` with bundled Python + deps.
6. **Multi-user** — Add auth, per-user sessions. Currently single-user by design.

## Notes for AG

### IMPERATIVE: Config-Driven Branding
**NEVER hardcode brand strings.** All of these must come from `config.json` `"branding"`:
- App name, tagline, copyright — in HTML, window titles, tray tooltips, logs
- Icon path — in tray.py and favicon
- Accent color — in CSS via JS override of `--accent` custom property
- The branding WILL change. If you hardcode "LoRaLink" anywhere in UI code, it will need to be found and replaced later. Use config from day one.

### IMPERATIVE: Offload to Ollama
**Use the local Ollama model (qwen2.5-coder:14b) for repetitive and boilerplate tasks.** AG should NOT hand-write everything — queue work to Ollama via the hybrid proxy for:
- **Boilerplate generation:** SQLite schema + CRUD methods, FastAPI route stubs, test file scaffolding
- **CSS generation:** Generate the full dark-theme stylesheet from the design tokens above
- **JS utilities:** WebSocket reconnection logic, fetch wrappers, DOM helpers
- **Repetitive patterns:** Error handler functions, logging setup, config loading
- **Test generation:** Given a function signature, generate pytest cases

Use `tools/hybrid_model_proxy.py` or the `ollama_bridge.py` to queue these. The model runs 24/7 at zero cost. AG's time is better spent on architecture decisions, integration, and validation — not typing boilerplate. This is the three-agent model: AG decides WHAT, Ollama generates HOW, AG reviews and integrates.

### Framework: Orion
- The Orion framework (multi-agent cooperative dev with RAG) is at `tools/multi-agent-framework/`. Reuse `rag/`, `hybrid_model_proxy.py`, and `rag/embeddings.py` directly — don't rewrite them.
- Orion's Garden at `C:\Users\spw1\Documents\Garden\` is a working proof-of-concept. `ask.py` demonstrates the full query pipeline.
- The daemon tray at `tools/daemon/tray.py` is the exact pattern for the system tray. Copy and modify.
- The webapp at `tools/webapp/` has the CSS design language to match. Pull colors and fonts from there.
- ChromaDB v1.5 requires `name()`, `embed_query()`, `get_config()`, `build_from_config()` on embedding functions. This is already handled in `tools/multi-agent-framework/rag/embeddings.py`.
- Ollama streaming uses `/api/generate` with `"stream": true`. Response is NDJSON.
- Bind to `127.0.0.1:8300`, never `0.0.0.0`.

### Suggested Ollama Offload Tasks (by implementation step)

| Step | What AG decides | What Ollama generates |
|------|----------------|----------------------|
| 1 | Server structure, config schema | FastAPI boilerplate, config loader, `/health` endpoint |
| 2 | Layout wireframe, design tokens | Full `style.css` from design tokens, HTML skeleton |
| 3 | SQLite schema design | `session_manager.py` CRUD methods, table creation SQL |
| 4 | Domain config format | `domain_manager.py` class with list/query/ingest methods |
| 5 | API contract (request/response) | Route handler with validation, error responses |
| 6 | UI interaction flow | `app.js` fetch wrapper, message rendering, DOM updates |
| 7 | WebSocket protocol design | WS handler with streaming loop, reconnection logic |
| 8 | Token rendering UX | JS streaming token renderer, typing animation |
| 9 | Voice UX (hold/release) | `voice.js` Web Speech API integration |
| 10 | Domain selector behavior | `domains.js` dropdown + ingest trigger |
| 11 | Tray menu structure | `tray.py` adapted from `tools/daemon/tray.py` |
| 12 | Startup sequence | `main.py` subprocess management, signal handling |
| 13 | Error scenario list | Error handler functions, user-facing messages |
| 14 | Logging strategy | Rotating file handler setup, graceful shutdown sequence |

**Pattern:** AG writes a 2-3 line prompt describing what's needed → Ollama generates 50-200 lines → AG reviews, integrates, tests. This is 3-5x faster than hand-writing.
