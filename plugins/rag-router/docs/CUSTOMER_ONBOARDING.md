# RAG Router — Customer Onboarding Guide

This guide walks through connecting a new customer project to the RAG Router platform.

## Overview

The RAG Router is a standalone platform. Each customer connects by providing:

1. A **Dify Knowledge Base** with their documents
2. A **customer `.env.rag`** file with their credentials
3. An **ingestion script** configured for their document sources

## Step-by-Step Setup

### 1. Prerequisites

Ensure the following are running on the customer's machine (or a shared server):

- **Dify** — with at least one LLM and one embedding model configured
- **Ollama** (if using local models) — with `nomic-embed-text` pulled
- **RAG Router** — the platform container

### 2. Create a Knowledge Base in Dify

1. Open the Dify dashboard
2. Go to **Knowledge** → **Create Knowledge**
3. Name it descriptively (e.g., "USVI Official Docs")
4. Note the **Dataset ID** from the URL:
   ```
   http://localhost:8400/datasets/<DATASET_ID>/documents
   ```

### 3. Generate an API Key

1. In Dify → **Settings** → **API Keys**
2. Create a new Dataset API key
3. Copy it (format: `dataset-xxxxxxxx`)

### 4. Configure the Embedding Model

1. In Dify → **Settings** → **Model Providers**
2. Add Ollama with URL: `http://host.docker.internal:11434`
3. Add `nomic-embed-text` as a **Text Embedding** model
4. Go to **System Model Settings** → set default **Embedding Model** to `nomic-embed-text`
5. **Save**

### 5. Create the Customer Config

In the customer's project directory, create `.env.rag`:

```env
DIFY_API_KEY=dataset-xxxxxxxx
DATASET_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
DIFY_BASE_URL=http://localhost:8400/v1
KNOWLEDGE_DIR=C:\path\to\customer\documents
RAG_ROUTER_DIR=C:\path\to\Antigravity\tools\rag_router
```

### 6. Prepare Documents

Place PDF/DOCX files in the `KNOWLEDGE_DIR` path. The ingestion script will upload all `.pdf` and `.docx` files found there.

### 7. Run Ingestion

```powershell
# From the customer's project directory
.\setup-rag.ps1

# Or skip ingestion if already done
.\setup-rag.ps1 -SkipIngestion
```

### 8. Verify

```bash
# Check RAG Router health
curl http://localhost:8403/health

# Check documents in Dify
# Open: http://localhost:8400/datasets/<DATASET_ID>/documents
# All documents should show status: "Available"
```

## Moving to Production

| What Changes | Local | Production |
|-------------|-------|------------|
| `DIFY_BASE_URL` | `http://localhost:8400/v1` | `https://dify.example.com/v1` |
| `DIFY_API_KEY` | Local dataset key | Production dataset key |
| `KNOWLEDGE_DIR` | Local filesystem path | Server path or S3 |
| Embedding model | Ollama (local) | OpenAI / cloud provider |

## Troubleshooting

| Error | Cause | Fix |
|-------|-------|-----|
| `provider_not_initialize` | No embedding model set | Set default embedding in Dify System Model Settings |
| `Default model not found for text-embedding` | Ollama not reachable from Dify | Use `http://host.docker.internal:11434` as Ollama URL |
| `Connection refused` on 8403 | RAG Router not running | `docker compose up -d --build` in RAG Router dir |
| `404` on Dify API | Wrong `DIFY_BASE_URL` | Check port and path (should end with `/v1`) |
