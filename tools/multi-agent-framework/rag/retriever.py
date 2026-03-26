#!/usr/bin/env python3
"""
RAG Retriever — queries the local ChromaDB vector store for relevant context.

Used by the hybrid model proxy to inject domain knowledge into prompts,
and can be used standalone for testing.

Usage:
    python -m rag.retriever --config config.json --query "how does mesh routing work"
"""

import argparse
import asyncio
import json
import sys
from pathlib import Path
from typing import Optional


def load_config(config_path: Optional[str] = None) -> dict:
    path = Path(config_path) if config_path else Path.cwd() / "config.json"
    if path.exists():
        with open(path) as f:
            return json.load(f)
    return {}


class Retriever:
    """Query ChromaDB for relevant document chunks."""

    def __init__(self, config: dict):
        self.config = config
        self._collection = None

    def _get_collection(self):
        if self._collection is not None:
            return self._collection

        import chromadb
        from rag.embeddings import OllamaEmbeddingFunction

        rag_cfg = self.config.get("rag", {})
        persist_dir = rag_cfg.get("persist_directory", ".rag_store")
        collection_name = rag_cfg.get("collection_name", "domain_knowledge")
        embedding_model = rag_cfg.get("embedding_model", "nomic-embed-text")
        ollama_url = self.config.get("model_proxy", {}).get("local", {}).get("base_url", "http://localhost:11434")

        embed_fn = OllamaEmbeddingFunction(model=embedding_model, base_url=ollama_url)
        client = chromadb.PersistentClient(path=str(Path(persist_dir).resolve()))
        self._collection = client.get_or_create_collection(
            name=collection_name,
            embedding_function=embed_fn,
        )
        return self._collection

    async def retrieve(self, query: str, top_k: Optional[int] = None) -> list[dict]:
        """Retrieve relevant document chunks for a query.

        Returns list of {"text": str, "source": str, "score": float}.
        """
        rag_cfg = self.config.get("rag", {})
        k = top_k or rag_cfg.get("top_k", 5)

        collection = self._get_collection()
        if collection.count() == 0:
            return []

        results = collection.query(query_texts=[query], n_results=min(k, collection.count()))

        docs = []
        for i, doc_text in enumerate(results["documents"][0]):
            metadata = results["metadatas"][0][i] if results.get("metadatas") else {}
            distance = results["distances"][0][i] if results.get("distances") else 0.0
            # ChromaDB returns squared L2 distance; lower = more similar
            # Convert to a readable similarity: 1/(1+d) maps [0,inf) -> (0,1]
            score = 1.0 / (1.0 + distance)
            docs.append({
                "text": doc_text,
                "source": metadata.get("source", "unknown"),
                "score": round(score, 4),
                "chunk_index": metadata.get("chunk_index", 0),
            })

        return docs


async def main():
    parser = argparse.ArgumentParser(description="Query RAG vector store")
    parser.add_argument("--config", help="Path to config.json")
    parser.add_argument("--query", required=True, help="Query text")
    parser.add_argument("--top-k", type=int, default=5, help="Number of results")
    args = parser.parse_args()

    config = load_config(args.config)
    retriever = Retriever(config)
    results = await retriever.retrieve(args.query, args.top_k)

    if not results:
        print("No results found. Have you ingested documents? Run: python -m rag.ingest --help")
        return

    print(f"\nTop {len(results)} results for: \"{args.query}\"\n")
    for i, doc in enumerate(results, 1):
        print(f"[{i}] Score: {doc['score']:.4f} | Source: {doc['source']}")
        preview = doc["text"][:200].replace("\n", " ")
        print(f"    {preview}...")
        print()


if __name__ == "__main__":
    asyncio.run(main())
