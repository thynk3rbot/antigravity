#!/usr/bin/env python3
"""
Hybrid Model Proxy - Route between local Ollama and OpenRouter/Cloud
Provides unified API interface with intelligent fallback and cost tracking
"""

import asyncio
import json
import logging
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any
from dataclasses import dataclass, asdict
import httpx

# Configure logging
log_dir = Path.home() / ".claude" / "hybrid_proxy"
log_dir.mkdir(parents=True, exist_ok=True)
log_file = log_dir / f"proxy_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(log_file),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

# Configuration
OLLAMA_BASE = "http://localhost:11434"
OPENROUTER_BASE = "https://openrouter.ai/api/v1"
OPENROUTER_KEY = None  # Set via env var or config

# Health check cache (in seconds)
HEALTH_CHECK_TTL = 30
last_health_check = {}
health_cache = {}


@dataclass
class RequestMetrics:
    """Track request metrics for reporting"""
    timestamp: str
    backend: str  # "ollama" or "openrouter"
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_tokens: int
    latency_ms: float
    cost_usd: float = 0.0
    status: str = "success"
    error: Optional[str] = None


class HybridModelProxy:
    """Route requests between Ollama and OpenRouter with fallback"""

    def __init__(self):
        self.metrics_log = []
        self.ollama_healthy = False
        self.openrouter_healthy = False
        self.openrouter_key = None

    async def check_ollama_health(self) -> bool:
        """Check if Ollama is running and healthy"""
        now = time.time()
        if "ollama" in health_cache and (now - last_health_check.get("ollama", 0)) < HEALTH_CHECK_TTL:
            return health_cache["ollama"]

        try:
            async with httpx.AsyncClient(timeout=5) as client:
                response = await client.get(f"{OLLAMA_BASE}/api/tags")
                is_healthy = response.status_code == 200
                health_cache["ollama"] = is_healthy
                last_health_check["ollama"] = now
                logger.info(f"Ollama health check: {'✓ HEALTHY' if is_healthy else '✗ DOWN'}")
                return is_healthy
        except Exception as e:
            logger.warning(f"Ollama health check failed: {e}")
            health_cache["ollama"] = False
            last_health_check["ollama"] = now
            return False

    async def check_openrouter_health(self) -> bool:
        """Check if OpenRouter is accessible"""
        if not self.openrouter_key:
            logger.warning("OpenRouter key not configured")
            return False

        now = time.time()
        if "openrouter" in health_cache and (now - last_health_check.get("openrouter", 0)) < HEALTH_CHECK_TTL:
            return health_cache["openrouter"]

        try:
            async with httpx.AsyncClient(timeout=5) as client:
                response = await client.get(
                    f"{OPENROUTER_BASE}/models",
                    headers={"Authorization": f"Bearer {self.openrouter_key}"}
                )
                is_healthy = response.status_code == 200
                health_cache["openrouter"] = is_healthy
                last_health_check["openrouter"] = now
                logger.info(f"OpenRouter health check: {'✓ HEALTHY' if is_healthy else '✗ DOWN'}")
                return is_healthy
        except Exception as e:
            logger.warning(f"OpenRouter health check failed: {e}")
            health_cache["openrouter"] = False
            last_health_check["openrouter"] = now
            return False

    async def query_ollama(self, model: str, prompt: str, **kwargs) -> Dict[str, Any]:
        """Query local Ollama instance"""
        logger.info(f"Routing to Ollama: {model}")
        start_time = time.time()

        try:
            async with httpx.AsyncClient(timeout=600) as client:
                response = await client.post(
                    f"{OLLAMA_BASE}/api/generate",
                    json={
                        "model": model,
                        "prompt": prompt,
                        "stream": False,
                        **kwargs
                    }
                )
                response.raise_for_status()
                result = response.json()
                latency_ms = (time.time() - start_time) * 1000

                # Log metrics
                metrics = RequestMetrics(
                    timestamp=datetime.now().isoformat(),
                    backend="ollama",
                    model=model,
                    prompt_tokens=result.get("prompt_eval_count", 0),
                    completion_tokens=result.get("eval_count", 0),
                    total_tokens=result.get("prompt_eval_count", 0) + result.get("eval_count", 0),
                    latency_ms=latency_ms,
                    cost_usd=0.0,  # Local is free
                    status="success"
                )
                self.metrics_log.append(metrics)
                logger.info(f"Ollama response: {metrics.total_tokens} tokens in {latency_ms:.0f}ms (FREE)")

                return {
                    "success": True,
                    "backend": "ollama",
                    "model": model,
                    "response": result.get("response", ""),
                    "metrics": asdict(metrics)
                }
        except Exception as e:
            logger.error(f"Ollama query failed: {e}")
            return {
                "success": False,
                "backend": "ollama",
                "error": str(e)
            }

    async def query_openrouter(self, model: str, prompt: str, **kwargs) -> Dict[str, Any]:
        """Query OpenRouter/Claude cloud"""
        logger.info(f"Routing to OpenRouter: {model}")
        start_time = time.time()

        if not self.openrouter_key:
            logger.error("OpenRouter key not configured")
            return {
                "success": False,
                "backend": "openrouter",
                "error": "OpenRouter key not configured"
            }

        try:
            async with httpx.AsyncClient(timeout=600) as client:
                response = await client.post(
                    f"{OPENROUTER_BASE}/chat/completions",
                    headers={
                        "Authorization": f"Bearer {self.openrouter_key}",
                        "HTTP-Referer": "https://github.com/antigravity",
                        "X-Title": "Antigravity-Phase50"
                    },
                    json={
                        "model": model,
                        "messages": [{"role": "user", "content": prompt}],
                        **kwargs
                    }
                )
                response.raise_for_status()
                result = response.json()
                latency_ms = (time.time() - start_time) * 1000

                # Extract tokens and cost
                usage = result.get("usage", {})
                prompt_tokens = usage.get("prompt_tokens", 0)
                completion_tokens = usage.get("completion_tokens", 0)
                total_tokens = prompt_tokens + completion_tokens

                # Estimate cost (rough, actual varies by model)
                cost_estimate = self._estimate_cost(model, prompt_tokens, completion_tokens)

                # Log metrics
                metrics = RequestMetrics(
                    timestamp=datetime.now().isoformat(),
                    backend="openrouter",
                    model=model,
                    prompt_tokens=prompt_tokens,
                    completion_tokens=completion_tokens,
                    total_tokens=total_tokens,
                    latency_ms=latency_ms,
                    cost_usd=cost_estimate,
                    status="success"
                )
                self.metrics_log.append(metrics)

                message = result["choices"][0]["message"]["content"]
                logger.info(f"OpenRouter response: {total_tokens} tokens in {latency_ms:.0f}ms (${cost_estimate:.4f})")

                return {
                    "success": True,
                    "backend": "openrouter",
                    "model": model,
                    "response": message,
                    "metrics": asdict(metrics)
                }
        except Exception as e:
            logger.error(f"OpenRouter query failed: {e}")
            return {
                "success": False,
                "backend": "openrouter",
                "error": str(e)
            }

    def _estimate_cost(self, model: str, prompt_tokens: int, completion_tokens: int) -> float:
        """Estimate cost based on model and token count"""
        # Pricing per 1M tokens (approximate)
        pricing = {
            "claude-3-opus": (0.015, 0.075),  # input, output
            "claude-3-sonnet": (0.003, 0.015),
            "gpt-4": (0.03, 0.06),
            "gpt-3.5-turbo": (0.0005, 0.0015),
        }

        if "opus" in model.lower():
            in_price, out_price = pricing["claude-3-opus"]
        elif "sonnet" in model.lower():
            in_price, out_price = pricing["claude-3-sonnet"]
        elif "gpt-4" in model.lower():
            in_price, out_price = pricing["gpt-4"]
        else:
            in_price, out_price = pricing["gpt-3.5-turbo"]

        return (prompt_tokens * in_price + completion_tokens * out_price) / 1_000_000

    async def query(self, model: str, prompt: str, prefer_local: bool = True, **kwargs) -> Dict[str, Any]:
        """
        Route query intelligently based on availability and preference

        Args:
            model: Model name (e.g., "qwen2.5-coder:14b" for Ollama, "claude-3-opus" for OpenRouter)
            prompt: Input prompt
            prefer_local: Try Ollama first if available (cheaper)
        """
        logger.info(f"Query received: {model} (prefer_local={prefer_local})")

        # Check health
        ollama_up = await self.check_ollama_health()
        openrouter_up = await self.check_openrouter_health()

        # Determine routing strategy
        if prefer_local and ollama_up:
            logger.info("→ Strategy: Local Ollama (preferred, available)")
            result = await self.query_ollama(model, prompt, **kwargs)
            if result["success"]:
                return result
            # Fallback to OpenRouter if Ollama fails
            if openrouter_up:
                logger.info("→ Fallback: OpenRouter (Ollama failed)")
                return await self.query_openrouter(model, prompt, **kwargs)

        elif ollama_up and not prefer_local:
            logger.info("→ Strategy: Ollama available (not preferred, but available)")
            return await self.query_ollama(model, prompt, **kwargs)

        else:
            # Use OpenRouter
            if openrouter_up:
                logger.info("→ Strategy: OpenRouter (local unavailable or not preferred)")
                return await self.query_openrouter(model, prompt, **kwargs)

        logger.error("✗ All backends unavailable!")
        return {
            "success": False,
            "error": "All backends unavailable (Ollama down, OpenRouter key missing/unreachable)"
        }

    def print_metrics_report(self):
        """Print summary of all requests and costs"""
        if not self.metrics_log:
            logger.info("No requests logged yet")
            return

        print("\n" + "=" * 80)
        print("HYBRID MODEL PROXY - METRICS REPORT")
        print("=" * 80)

        # Group by backend
        ollama_requests = [m for m in self.metrics_log if m.backend == "ollama"]
        openrouter_requests = [m for m in self.metrics_log if m.backend == "openrouter"]

        print(f"\nTotal Requests: {len(self.metrics_log)}")
        print(f"  Ollama: {len(ollama_requests)} (FREE)")
        print(f"  OpenRouter: {len(openrouter_requests)}")

        if ollama_requests:
            total_tokens_ollama = sum(m.total_tokens for m in ollama_requests)
            avg_latency_ollama = sum(m.latency_ms for m in ollama_requests) / len(ollama_requests)
            print(f"\nOllama Summary:")
            print(f"  Total tokens: {total_tokens_ollama:,}")
            print(f"  Avg latency: {avg_latency_ollama:.0f}ms")
            print(f"  Cost: $0.00 (LOCAL)")

        if openrouter_requests:
            total_tokens_openrouter = sum(m.total_tokens for m in openrouter_requests)
            total_cost = sum(m.cost_usd for m in openrouter_requests)
            avg_latency_openrouter = sum(m.latency_ms for m in openrouter_requests) / len(openrouter_requests)
            print(f"\nOpenRouter Summary:")
            print(f"  Total tokens: {total_tokens_openrouter:,}")
            print(f"  Avg latency: {avg_latency_openrouter:.0f}ms")
            print(f"  Total cost: ${total_cost:.4f}")

        total_cost = sum(m.cost_usd for m in self.metrics_log)
        total_tokens = sum(m.total_tokens for m in self.metrics_log)
        print(f"\nOverall:")
        print(f"  Total tokens: {total_tokens:,}")
        print(f"  Total cost: ${total_cost:.4f}")
        print(f"  Savings (local vs cloud): ${total_tokens * 0.00003:.4f} (estimated)")
        print("=" * 80 + "\n")


async def main():
    """Demo / test mode"""
    proxy = HybridModelProxy()
    proxy.openrouter_key = "dummy-key-for-demo"  # Replace with real key

    logger.info("Starting Hybrid Model Proxy...")
    logger.info(f"Ollama endpoint: {OLLAMA_BASE}")
    logger.info(f"OpenRouter endpoint: {OPENROUTER_BASE}")

    # Test 1: Try Ollama (should work if running)
    print("\n[TEST 1] Querying Ollama (if available)...")
    result = await proxy.query(
        model="qwen2.5-coder:14b",
        prompt="Write a Python function to check if a number is prime",
        prefer_local=True
    )
    print(f"Result: {result['success']}")
    if result['success']:
        print(f"Backend: {result['backend']}")
        print(f"Response preview: {result['response'][:200]}...")

    # Test 2: Health status
    print("\n[TEST 2] Health Status Check...")
    ollama_healthy = await proxy.check_ollama_health()
    openrouter_healthy = await proxy.check_openrouter_health()
    print(f"Ollama: {'✓' if ollama_healthy else '✗'}")
    print(f"OpenRouter: {'✓' if openrouter_healthy else '✗'}")

    # Test 3: Metrics report
    print("\n[TEST 3] Metrics Report...")
    proxy.print_metrics_report()

    logger.info("Hybrid Model Proxy demo complete")
    logger.info(f"Logs saved to: {log_file}")


if __name__ == "__main__":
    asyncio.run(main())
