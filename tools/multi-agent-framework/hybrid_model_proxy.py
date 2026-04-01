#!/usr/bin/env python3
"""
Hybrid Model Proxy — Route between local Ollama and cloud providers
with optional RAG context injection.

Reads configuration from config.json for endpoints, models, and pricing.
Falls back to sensible defaults if no config is found.

Usage:
    python hybrid_model_proxy.py                        # Run demo/health check
    python hybrid_model_proxy.py --config config.json   # Use specific config
"""

import asyncio
import json
import logging
import os
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any
from dataclasses import dataclass, asdict

import httpx

# ── Configuration ────────────────────────────────────────────────────────────

DEFAULT_CONFIG = {
    "project": {"name": "Orion Instance"},
    "branding": {"app_name": "Orion", "copyright": ""},
    "model_proxy": {
        "local": {
            "provider": "ollama",
            "base_url": "http://localhost:11434",
            "default_model": "qwen2.5-coder:14b",
        },
        # Free providers — priority order: groq → gemini → cerebras → openrouter:free → openrouter:paid
        "providers": {
            "groq": {
                "base_url": "https://api.groq.com/openai/v1",
                "api_key_env": "GROQ_API_KEY",
                "default_model": "llama-3.3-70b-versatile",
                "free": True,
                "models": [
                    "llama-3.3-70b-versatile",   # best free general model
                    "llama-3.1-8b-instant",       # fast/cheap
                    "gemma2-9b-it",
                    "mixtral-8x7b-32768",
                ],
            },
            "gemini": {
                "base_url": "https://generativelanguage.googleapis.com/v1beta/openai",
                "api_key_env": "GEMINI_API_KEY",
                "default_model": "gemini-2.0-flash",
                "free": True,
                "models": [
                    "gemini-2.0-flash",           # best free, 1M context
                    "gemini-1.5-flash",
                ],
            },
            "cerebras": {
                "base_url": "https://api.cerebras.ai/v1",
                "api_key_env": "CEREBRAS_API_KEY",
                "default_model": "llama-3.3-70b",
                "free": True,
                "models": [
                    "llama-3.3-70b",              # 2000 tok/s — fastest available
                    "llama-3.1-8b",
                ],
            },
            "openrouter": {
                "base_url": "https://openrouter.ai/api/v1",
                "api_key_env": "OPENROUTER_API_KEY",
                "default_model": "meta-llama/llama-3.1-8b-instruct:free",
                "free_models": [
                    "meta-llama/llama-3.1-8b-instruct:free",
                    "google/gemma-2-9b-it:free",
                    "mistralai/mistral-7b-instruct:free",
                    "microsoft/phi-3-mini-128k-instruct:free",
                ],
                "paid_fallback": "anthropic/claude-3-sonnet",
            },
        },
        # Legacy single-cloud config (kept for backward compat)
        "cloud": {
            "provider": "openrouter",
            "base_url": "https://openrouter.ai/api/v1",
            "api_key_env": "OPENROUTER_API_KEY",
            "default_model": "meta-llama/llama-3.1-8b-instruct:free",
        },
        "prefer_local": True,
        "prefer_free": True,        # try free providers before paid
        "free_provider_order": ["groq", "gemini", "cerebras", "openrouter"],
        "health_check_ttl": 30,
        "pricing": {
            "claude-3-opus": [0.015, 0.075],
            "claude-3-sonnet": [0.003, 0.015],
            "gpt-4": [0.03, 0.06],
            "gpt-3.5-turbo": [0.0005, 0.0015],
            "llama-3.3-70b-versatile": [0.0, 0.0],   # groq free
            "gemini-2.0-flash": [0.0, 0.0],           # gemini free tier
            "llama-3.3-70b": [0.0, 0.0],              # cerebras free tier
        },
    },
    "rag": {"enabled": False},
}


def load_config(config_path=None):
    path = Path(config_path) if config_path else Path.cwd() / "config.json"
    if path.exists():
        try:
            with open(path) as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError):
            pass
    return DEFAULT_CONFIG


# ── Logging ──────────────────────────────────────────────────────────────────

log_dir = Path.home() / ".multi-agent" / "proxy_logs"
log_dir.mkdir(parents=True, exist_ok=True)
log_file = log_dir / f"proxy_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(levelname)s - %(message)s",
    handlers=[logging.FileHandler(log_file), logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger(__name__)

# Health check cache
_health_cache = {}
_health_ts = {}


# ── Metrics ──────────────────────────────────────────────────────────────────

@dataclass
class RequestMetrics:
    timestamp: str
    backend: str
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_tokens: int
    latency_ms: float
    cost_usd: float = 0.0
    status: str = "success"
    error: Optional[str] = None
    rag_context_used: bool = False


# ── Proxy ────────────────────────────────────────────────────────────────────

class HybridModelProxy:
    def __init__(self, config=None):
        self.config = config or DEFAULT_CONFIG
        self.metrics_log = []
        self._retriever = None

        proxy_cfg = self.config.get("model_proxy", {})
        self.ollama_base = proxy_cfg.get("local", {}).get("base_url", "http://localhost:11434")
        self.openrouter_base = proxy_cfg.get("cloud", {}).get("base_url", "https://openrouter.ai/api/v1")
        self.openrouter_key = os.environ.get(
            proxy_cfg.get("cloud", {}).get("api_key_env", "OPENROUTER_API_KEY"), ""
        )
        self.health_ttl = proxy_cfg.get("health_check_ttl", 30)
        self.project_name = self.config.get("project", {}).get("name", "MultiAgentProject")
        self.pricing = proxy_cfg.get("pricing", DEFAULT_CONFIG["model_proxy"]["pricing"])

    # ── RAG integration ──────────────────────────────────────────────────

    async def _get_rag_context(self, prompt: str) -> Optional[str]:
        """Retrieve RAG context if enabled. Lazy-loads retriever."""
        rag_cfg = self.config.get("rag", {})
        if not rag_cfg.get("enabled", False):
            return None
        try:
            if self._retriever is None:
                from rag.retriever import Retriever
                self._retriever = Retriever(self.config)
            docs = await self._retriever.retrieve(prompt)
            if docs:
                return "\n\n".join(d["text"] for d in docs)
        except ImportError:
            logger.warning("RAG modules not installed — skipping context injection")
        except Exception as e:
            logger.warning(f"RAG retrieval failed: {e}")
        return None

    # ── Health checks ────────────────────────────────────────────────────

    async def check_ollama_health(self) -> bool:
        now = time.time()
        if "ollama" in _health_cache and (now - _health_ts.get("ollama", 0)) < self.health_ttl:
            return _health_cache["ollama"]
        try:
            async with httpx.AsyncClient(timeout=5) as client:
                resp = await client.get(f"{self.ollama_base}/api/tags")
                healthy = resp.status_code == 200
        except Exception:
            healthy = False
        _health_cache["ollama"] = healthy
        _health_ts["ollama"] = now
        logger.info(f"Ollama health: {'OK' if healthy else 'DOWN'}")
        return healthy

    async def check_openrouter_health(self) -> bool:
        if not self.openrouter_key:
            return False
        now = time.time()
        if "openrouter" in _health_cache and (now - _health_ts.get("openrouter", 0)) < self.health_ttl:
            return _health_cache["openrouter"]
        try:
            async with httpx.AsyncClient(timeout=5) as client:
                resp = await client.get(
                    f"{self.openrouter_base}/models",
                    headers={"Authorization": f"Bearer {self.openrouter_key}"},
                )
                healthy = resp.status_code == 200
        except Exception:
            healthy = False
        _health_cache["openrouter"] = healthy
        _health_ts["openrouter"] = now
        logger.info(f"OpenRouter health: {'OK' if healthy else 'DOWN'}")
        return healthy

    # ── Query backends ───────────────────────────────────────────────────

    async def query_ollama(self, model: str, prompt: str, **kwargs) -> Dict[str, Any]:
        logger.info(f"Routing to Ollama: {model}")
        start = time.time()
        try:
            async with httpx.AsyncClient(timeout=600) as client:
                resp = await client.post(
                    f"{self.ollama_base}/api/generate",
                    json={"model": model, "prompt": prompt, "stream": False, **kwargs},
                )
                resp.raise_for_status()
                result = resp.json()
                latency = (time.time() - start) * 1000
                metrics = RequestMetrics(
                    timestamp=datetime.now().isoformat(), backend="ollama", model=model,
                    prompt_tokens=result.get("prompt_eval_count", 0),
                    completion_tokens=result.get("eval_count", 0),
                    total_tokens=result.get("prompt_eval_count", 0) + result.get("eval_count", 0),
                    latency_ms=latency, cost_usd=0.0,
                )
                self.metrics_log.append(metrics)
                logger.info(f"Ollama: {metrics.total_tokens} tokens in {latency:.0f}ms (FREE)")
                return {"success": True, "backend": "ollama", "model": model,
                        "response": result.get("response", ""), "metrics": asdict(metrics)}
        except Exception as e:
            logger.error(f"Ollama query failed: {e}")
            return {"success": False, "backend": "ollama", "error": str(e)}

    async def query_openrouter(self, model: str, prompt: str, **kwargs) -> Dict[str, Any]:
        logger.info(f"Routing to OpenRouter: {model}")
        start = time.time()
        if not self.openrouter_key:
            return {"success": False, "backend": "openrouter", "error": "API key not configured"}
        try:
            async with httpx.AsyncClient(timeout=600) as client:
                resp = await client.post(
                    f"{self.openrouter_base}/chat/completions",
                    headers={
                        "Authorization": f"Bearer {self.openrouter_key}",
                        "HTTP-Referer": "https://github.com/multi-agent-framework",
                        "X-Title": self.project_name,
                    },
                    json={"model": model, "messages": [{"role": "user", "content": prompt}], **kwargs},
                )
                resp.raise_for_status()
                result = resp.json()
                latency = (time.time() - start) * 1000
                usage = result.get("usage", {})
                pt = usage.get("prompt_tokens", 0)
                ct = usage.get("completion_tokens", 0)
                cost = self._estimate_cost(model, pt, ct)
                metrics = RequestMetrics(
                    timestamp=datetime.now().isoformat(), backend="openrouter", model=model,
                    prompt_tokens=pt, completion_tokens=ct, total_tokens=pt + ct,
                    latency_ms=latency, cost_usd=cost,
                )
                self.metrics_log.append(metrics)
                message = result["choices"][0]["message"]["content"]
                logger.info(f"OpenRouter: {pt + ct} tokens in {latency:.0f}ms (${cost:.4f})")
                return {"success": True, "backend": "openrouter", "model": model,
                        "response": message, "metrics": asdict(metrics)}
        except Exception as e:
            logger.error(f"OpenRouter query failed: {e}")
            return {"success": False, "backend": "openrouter", "error": str(e)}

    def _estimate_cost(self, model: str, prompt_tokens: int, completion_tokens: int) -> float:
        for key, (in_price, out_price) in self.pricing.items():
            if key.lower() in model.lower():
                return (prompt_tokens * in_price + completion_tokens * out_price) / 1_000_000
        return 0.0

    # ── Main query router (with RAG injection) ───────────────────────────

    async def stream(self, model: str, prompt: str, **kwargs):
        """Yield tokens as they arrive from Ollama."""
        # Note: Currently only local Ollama supports streaming in this implementation
        async with httpx.AsyncClient() as client:
            try:
                async with client.stream(
                    "POST",
                    f"{self.ollama_base}/api/generate",
                    json={"model": model, "prompt": prompt, "stream": True, **kwargs},
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
            except Exception as e:
                logger.error(f"Streaming failed: {e}")
                yield {"error": str(e)}

    async def query(self, model: str, prompt: str, prefer_local: bool = True,
                    use_rag: Optional[bool] = None, **kwargs) -> Dict[str, Any]:
        """Route query with optional RAG context injection."""
        logger.info(f"Query: model={model}, prefer_local={prefer_local}")

        # RAG context injection
        rag_used = False
        if use_rag is True or (use_rag is None and self.config.get("rag", {}).get("enabled", False)):
            context = await self._get_rag_context(prompt)
            if context:
                prompt = f"Relevant context from knowledge base:\n{context}\n\n---\nQuery: {prompt}"
                rag_used = True
                logger.info("RAG context injected into prompt")

        # Health checks
        ollama_up = await self.check_ollama_health()
        openrouter_up = await self.check_openrouter_health()

        # Route
        if prefer_local and ollama_up:
            result = await self.query_ollama(model, prompt, **kwargs)
            if result["success"]:
                if rag_used:
                    result["rag_context_used"] = True
                return result
            if openrouter_up:
                logger.info("Fallback: OpenRouter (Ollama failed)")
                result = await self.query_openrouter(model, prompt, **kwargs)
                if rag_used:
                    result["rag_context_used"] = True
                return result
        elif ollama_up:
            result = await self.query_ollama(model, prompt, **kwargs)
            if rag_used:
                result["rag_context_used"] = True
            return result
        elif openrouter_up:
            result = await self.query_openrouter(model, prompt, **kwargs)
            if rag_used:
                result["rag_context_used"] = True
            return result

        return {"success": False, "error": "All backends unavailable"}

    # ── Metrics report ───────────────────────────────────────────────────

    def print_metrics_report(self):
        if not self.metrics_log:
            logger.info("No requests logged yet")
            return
        print("HYBRID MODEL PROXY — METRICS REPORT")
        print("=" * 70)
        print("=" * 70)
        ollama_reqs = [m for m in self.metrics_log if m.backend == "ollama"]
        cloud_reqs = [m for m in self.metrics_log if m.backend != "ollama"]
        print(f"\nTotal Requests: {len(self.metrics_log)}")
        print(f"  Local: {len(ollama_reqs)} (FREE)")
        print(f"  Cloud: {len(cloud_reqs)}")
        if ollama_reqs:
            total_t = sum(m.total_tokens for m in ollama_reqs)
            avg_l = sum(m.latency_ms for m in ollama_reqs) / len(ollama_reqs)
            print(f"\nLocal Summary:\n  Tokens: {total_t:,}\n  Avg latency: {avg_l:.0f}ms\n  Cost: $0.00")
        if cloud_reqs:
            total_t = sum(m.total_tokens for m in cloud_reqs)
            total_c = sum(m.cost_usd for m in cloud_reqs)
            avg_l = sum(m.latency_ms for m in cloud_reqs) / len(cloud_reqs)
            print(f"\nCloud Summary:\n  Tokens: {total_t:,}\n  Avg latency: {avg_l:.0f}ms\n  Cost: ${total_c:.4f}")
        rag_count = sum(1 for m in self.metrics_log if m.rag_context_used)
        if rag_count:
            print(f"\nRAG-augmented queries: {rag_count}")
        print(f"{'=' * 70}\n")


# ── CLI ──────────────────────────────────────────────────────────────────────

async def main():
    config_path = None
    if "--config" in sys.argv:
        idx = sys.argv.index("--config")
        if idx + 1 < len(sys.argv):
            config_path = sys.argv[idx + 1]

    config = load_config(config_path)
    proxy_cfg = config.get("model_proxy", {})
    local_model = proxy_cfg.get("local", {}).get("default_model", "qwen2.5-coder:14b")

    proxy = HybridModelProxy(config)
    logger.info("Starting Hybrid Model Proxy...")
    logger.info(f"Local: {proxy.ollama_base}")
    logger.info(f"Cloud: {proxy.openrouter_base}")
    logger.info(f"RAG: {'enabled' if config.get('rag', {}).get('enabled') else 'disabled'}")

    print("\n[Health Check]")
    ollama_ok = await proxy.check_ollama_health()
    openrouter_ok = await proxy.check_openrouter_health()
    print(f"  Ollama: {'OK' if ollama_ok else 'DOWN'}")
    print(f"  OpenRouter: {'OK' if openrouter_ok else 'DOWN'}")

    if ollama_ok:
        print(f"\n[Test Query] Sending to {local_model}...")
        result = await proxy.query(model=local_model, prompt="Say hello in one sentence.", prefer_local=True)
        print(f"  Success: {result['success']}")
        if result.get("success"):
            print(f"  Backend: {result['backend']}")
            print(f"  Response: {result['response'][:200]}")

    proxy.print_metrics_report()
    logger.info(f"Logs: {log_file}")


if __name__ == "__main__":
    asyncio.run(main())
