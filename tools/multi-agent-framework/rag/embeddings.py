#!/usr/bin/env python3
"""
Local Embeddings via Ollama — generates vector embeddings using models like
nomic-embed-text, running entirely on local hardware at zero cost.

Requires Ollama running with an embedding model pulled:
    ollama pull nomic-embed-text
"""

import httpx

DEFAULT_MODEL = "nomic-embed-text"
DEFAULT_BASE_URL = "http://localhost:11434"


async def embed(text: str, model: str = DEFAULT_MODEL,
                base_url: str = DEFAULT_BASE_URL) -> list[float]:
    """Generate embedding for a single text string."""
    async with httpx.AsyncClient(timeout=60) as client:
        resp = await client.post(
            f"{base_url}/api/embeddings",
            json={"model": model, "prompt": text},
        )
        resp.raise_for_status()
        return resp.json()["embedding"]


async def embed_batch(texts: list[str], model: str = DEFAULT_MODEL,
                      base_url: str = DEFAULT_BASE_URL) -> list[list[float]]:
    """Generate embeddings for a batch of texts (sequential calls to Ollama)."""
    results = []
    async with httpx.AsyncClient(timeout=60) as client:
        for text in texts:
            resp = await client.post(
                f"{base_url}/api/embeddings",
                json={"model": model, "prompt": text},
            )
            resp.raise_for_status()
            results.append(resp.json()["embedding"])
    return results


class OllamaEmbeddingFunction:
    """ChromaDB-compatible embedding function using local Ollama."""

    def __init__(self, model: str = DEFAULT_MODEL, base_url: str = DEFAULT_BASE_URL):
        self.model = model
        self.base_url = base_url

    def __call__(self, input: list[str]) -> list[list[float]]:
        """Synchronous interface required by ChromaDB."""
        results = []
        with httpx.Client(timeout=60) as client:
            for text in input:
                resp = client.post(
                    f"{self.base_url}/api/embeddings",
                    json={"model": self.model, "prompt": text},
                )
                resp.raise_for_status()
                results.append(resp.json()["embedding"])
        return results
