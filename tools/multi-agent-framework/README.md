# Orion — Multi-Agent Cooperative Development Framework

A reusable framework for coordinating multiple AI agents (Claude, Ollama, IDE agents, etc.) working on the same codebase. Enforces structured handoffs, file ownership, and optional RAG-augmented local model queries.

> **Branding note:** All user-facing strings (app name, tagline, copyright, colors) are driven by the `"branding"` section in `config.json`. Never hardcode brand identity in code — always read from config. This allows any Orion instance to be rebranded without code changes.

## Quick Start

```bash
# 1. Configure your project
cp config.example.json config.json
# Edit config.json with your agent names, directories, and model settings

# 2. Scaffold a new project
python init.py --config config.json --target /path/to/new/project

# 3. Install dependencies
pip install -r requirements.txt

# 4. Start working
python tools/agent_tracking.py acquire Claude "Implementing feature X"
# ... do work ...
python tools/agent_tracking.py release Claude
```

## Architecture

### Three-Phase Tollbooth Model

```
Phase 1: PLANNING          Phase 2: EXECUTION         Phase 3: REVIEW
  Planner Agent        ->    Executor Agent       ->    Reviewer Agent
  spec.md output             source files               audit_report.md
  /01_planning/              /02_coding/                /03_review/
```

Each phase has an assigned agent, a dedicated directory, and a lock file preventing concurrent access. Work flows linearly from planning through review.

### Hybrid Model Proxy

Routes queries between local Ollama (free) and cloud providers (fallback) with cost tracking:

```python
proxy = HybridModelProxy(config)
result = await proxy.query(
    model="qwen2.5-coder:14b",
    prompt="Generate a REST endpoint...",
    prefer_local=True,    # Try Ollama first
    use_rag=True,         # Inject domain context
)
```

### RAG Domain Augmentation

Ingest domain documents into a local ChromaDB vector store, then the proxy automatically injects relevant context into prompts:

```bash
# Pull the embedding model
ollama pull nomic-embed-text

# Ingest your domain docs
python -m rag.ingest --config config.json --source docs/ specs/

# Test retrieval
python -m rag.retriever --config config.json --query "how does authentication work"

# Enable in config.json: "rag": { "enabled": true, ... }
# Now all proxy queries get domain context automatically
```

## Tools

| Tool | Purpose |
|------|---------|
| `agent_tracking.py` | Lock management, file ownership, audit trail |
| `hybrid_model_proxy.py` | Intelligent model routing with RAG injection |
| `ollama_bridge.py` | Delegate tasks to local Ollama models |
| `rag/ingest.py` | Chunk and store documents in ChromaDB |
| `rag/retriever.py` | Query the vector store for relevant context |
| `rag/embeddings.py` | Local embedding generation via Ollama |
| `init.py` | Scaffold a new project from config |

## Configuration Reference

See `config.example.json` for the full schema. Key sections:

- **`project`**: Name, description, build/test commands
- **`branding`**: App name, tagline, copyright, icon path, accent color, theme — all UI-facing identity
- **`agents`**: Define your 3 agents (name, tool, lock file, directory scopes)
- **`phases`**: Map phases to directories and I/O artifacts
- **`model_proxy`**: Local/cloud endpoints, pricing, health check TTL
- **`rag`**: Vector store settings, embedding model, chunk size, sources
- **`lock_settings`**: Timeout, directory, audit log path

## Adapting to Your Domain

1. Copy `config.example.json` and fill in your project details
2. Run `python init.py --config your_config.json --target /your/project`
3. Customize the generated docs in `docs/` for your team
4. If using RAG, ingest your domain documents and enable in config
5. Each agent reads `AGENT_ASSIGNMENTS.md` before starting work

## Examples

See `examples/magic/config.json` for a real-world configuration used in ESP32 firmware development with Claude + Ollama + Antigravity IDE.

### Known Orion Instances

| Instance | Domain | Location |
|----------|--------|----------|
| Magic | ESP32 mesh firmware | This repo (`examples/magic/`) |
| Orion's Garden | Gardening knowledge base | `C:\Users\spw1\Documents\Garden\` |
| Magic Assistant | Multi-domain AI chat | `tools/assistant/` (planned) |
