# RAG Router — Platform Setup Guide

> Universal IoT-to-RAG routing microservice.
> Pipeline: MQTT Ingest → Domain Classification → Completeness Guard → Dify RAG → Response

## Prerequisites

| Requirement | Version | Check |
|-------------|---------|-------|
| Docker Desktop | 4.x+ | `docker --version` |
| Python | 3.10+ | `python --version` |
| Dify | 1.x+ | Running on a known port |
| Ollama (local dev) | 0.3+ | `ollama --version` |

## Quick Start

```bash
cd tools/rag_router
cp .env.example .env        # Edit with your Dify URL
docker compose up -d --build
```

Verify: `curl http://localhost:8403/health` → `{"status":"ok"}`

## Architecture

```
┌─────────────┐    ┌──────────────┐    ┌───────────┐
│ IoT Devices │───▶│  MQTT (EMQX) │───▶│           │
└─────────────┘    └──────────────┘    │           │
                                       │ RAG Router│───▶ Dify API
┌─────────────┐                        │ :8403     │    (any host)
│  Browser /  │───▶ HTTP POST ────────▶│           │
│  WordPress  │                        └───────────┘
└─────────────┘
```

## Service Ports

| Service | Default Port | Configurable Via |
|---------|-------------|------------------|
| RAG Router | 8403 | `RAG_ROUTER_PORT` in `.env` |
| MQTT (external) | 1884 | `docker-compose.yml` |
| EMQX Dashboard | 18084 | `docker-compose.yml` |

## Configuration (.env)

```env
# Dify RAG Engine — point to any Dify instance
DIFY_BASE_URL=http://host.docker.internal:8400/v1
DIFY_API_KEY=

# MQTT Broker (internal service name)
MQTT_BROKER=emqx
MQTT_PORT=1883

# Server
RAG_ROUTER_PORT=8403
LOG_LEVEL=INFO
```

## API Reference

### Health Check

```
GET /health → {"status": "ok", "service": "rag_router"}
```

### Ingest Sensor Data

```
POST /api/ingest
{
  "id": "PH-01",
  "hwType": "ph-ec-sensor",
  "data": {"ph": 6.1, "ec": 1.8}
}
```

### Ingest + Query (one-shot)

```
POST /api/ingest-and-query
{
  "id": "PH-01",
  "hwType": "ph-ec-sensor",
  "data": {"ph": 6.1, "ec": 1.8}
}
→ {"domain": "NUTRIENT", "answer": "...", "sources": [...]}
```

### Register Custom Domain

```
POST /api/products/register
{
  "domain": "USVI",
  "knowledge_id": "dataset-xxxx",
  "keywords": ["usvi", "virgin islands", "government"],
  "expert_prompt": "You are a USVI policy expert..."
}
```

### WebSocket (real-time)

```
ws://localhost:8403/ws
→ Send: {"action": "query", "node_id": "PH-01"}
← Recv: {"event": "query_result", "data": {...}}
```

## Deployment

### Local Development

```bash
docker compose up -d --build
```

### Production

1. Copy `.env.production` → `.env`
2. Update `DIFY_BASE_URL` to your hosted Dify
3. Update `MQTT_BROKER` to your hosted EMQX
4. `docker compose up -d --build`

## File Structure

```
tools/rag_router/
├── server.py              # FastAPI application
├── Dockerfile             # Container build
├── docker-compose.yml     # Stack definition (RAG Router + EMQX only)
├── requirements.txt       # Python dependencies
├── .env                   # Active config (git-ignored)
├── .env.example           # Template for new setups
├── .env.production        # Template for production
├── static/
│   ├── index.html         # Dashboard UI
│   └── test.html          # Test console
└── scripts/
    └── ingest_viai_knowledge.py  # Batch document uploader
```
