# Claude API Integration Guide

This guide explains how to set up and use Claude API (Anthropic) in your Magic project.

## Quick Start

### 1. Get Your API Key

1. Go to **https://console.anthropic.com/account/keys**
2. Sign in with your Anthropic account (create one if needed)
3. Click **"Create Key"** and copy it
4. You now have your `ANTHROPIC_API_KEY`

### 2. Add the Key to Your Environment

**Option A: Add to `.env` file (Recommended)**

Edit `tools/rag_router/.env`:
```bash
ANTHROPIC_API_KEY=sk-ant-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

**Option B: Set as environment variable (Bash/Linux/Mac)**
```bash
export ANTHROPIC_API_KEY="sk-ant-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

**Option C: Set as environment variable (Windows PowerShell)**
```powershell
$env:ANTHROPIC_API_KEY="sk-ant-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

### 3. Verify Your Setup

Run the health check:
```bash
cd tools/multi-agent-framework
python hybrid_model_proxy.py
```

You should see:
```
[Health Check]
  Ollama: DOWN (or OK)
  OpenRouter: OK
  Anthropic: OK  ← This should say OK
```

## Using Claude API

### In Python Code

```python
from tools.multi-agent-framework.hybrid_model_proxy import HybridModelProxy
import asyncio

async def main():
    proxy = HybridModelProxy()

    # Use Claude directly
    result = await proxy.query(
        model="claude-3-5-sonnet-20241022",
        prompt="Explain the difference between synchronous and asynchronous code.",
        backend="anthropic"  # Force use of Anthropic
    )

    print(result["response"])
    print(f"Cost: ${result['metrics']['cost_usd']:.4f}")

asyncio.run(main())
```

### Let the Proxy Choose (Smart Routing)

```python
# Prefers Ollama (local), falls back to Anthropic, then OpenRouter
result = await proxy.query(
    model="claude-3-5-sonnet-20241022",
    prompt="Your prompt here"
)
```

### Force Anthropic as Fallback

```python
# Uses local Ollama if available, otherwise falls back to Anthropic
result = await proxy.query(
    model="claude-3-5-sonnet-20241022",
    prompt="Your prompt here",
    prefer_local=True
)
```

## Configuration Files

### `tools/multi-agent-framework/config.example.json`

Shows the Anthropic section:
```json
"anthropic": {
  "provider": "anthropic",
  "base_url": "https://api.anthropic.com",
  "api_key_env": "ANTHROPIC_API_KEY",
  "default_model": "claude-3-5-sonnet-20241022"
}
```

### `tools/rag_router/.env`

Contains your actual credentials:
```
ANTHROPIC_API_KEY=sk-ant-xxxxx
```

## Available Claude Models

As of 2026-03, available models include:

- **claude-3-5-sonnet-20241022** (Recommended for most tasks)
  - Good balance of speed and capability
  - Mid-tier cost

- **claude-3-opus-20250219**
  - Most capable, higher cost
  - Best for complex reasoning

- **claude-3-haiku-20250307**
  - Fastest, lowest cost
  - Good for simple tasks

For current models, check: https://docs.anthropic.com/claude/reference/models-overview

## When Claude API Runs Out of Tokens

The hybrid proxy automatically falls back:

1. **Try Local Ollama** (free, if running)
2. **Fallback to Anthropic** (your purchased tokens)
3. **Final fallback to OpenRouter** (alternative cloud provider)

You can force a specific backend:

```python
# Skip to OpenRouter if you want to preserve Claude tokens
result = await proxy.query(
    model="gpt-4",
    prompt="Your prompt",
    backend="openrouter"
)
```

## Pricing Reference

Claude API pricing (as of 2026):

- **Claude 3.5 Sonnet**
  - Input: $3.00 per 1M tokens
  - Output: $15.00 per 1M tokens

- **Claude 3 Opus**
  - Input: $15.00 per 1M tokens
  - Output: $75.00 per 1M tokens

- **Claude 3 Haiku**
  - Input: $0.80 per 1M tokens
  - Output: $4.00 per 1M tokens

For current pricing: https://www.anthropic.com/pricing/claude

## Monitoring Usage

Each query logs metrics:

```python
result = await proxy.query(model="claude-3-5-sonnet-20241022", prompt="Your prompt")

metrics = result['metrics']
print(f"Tokens: {metrics['total_tokens']}")
print(f"Cost: ${metrics['cost_usd']:.6f}")
print(f"Latency: {metrics['latency_ms']:.0f}ms")
print(f"Backend: {metrics['backend']}")
```

View all metrics:
```python
proxy.print_metrics_report()
```

## Security Best Practices

1. **Never commit API keys to git**
   - `.gitignore` already includes `.env`
   - Use `.env.example` for templates only

2. **Rotate keys periodically**
   - Go to https://console.anthropic.com/account/keys
   - Delete old keys, create new ones
   - Update `.env` with new key

3. **Use environment variables, not hardcoded keys**
   ```python
   # ✅ Good
   key = os.getenv("ANTHROPIC_API_KEY")

   # ❌ Bad
   key = "sk-ant-xxxxx"  # Don't hardcode!
   ```

4. **Monitor usage on Anthropic Console**
   - Check https://console.anthropic.com/dashboard
   - Set spending limits if needed

## Troubleshooting

### "API key not configured" Error

Make sure your `.env` file has:
```
ANTHROPIC_API_KEY=sk-ant-xxxxxxxxxxxxx
```

And you're running from the correct directory where `.env` is located.

### "Anthropic: DOWN" in Health Check

1. Check your internet connection
2. Verify API key is correct
3. Check Anthropic status: https://status.anthropic.com/
4. Make sure your API key hasn't expired/been revoked

### High Latency or Slow Responses

Claude API is typically 1-3 seconds per request. If slower:
- Check your internet speed
- Try a different model (Haiku is fastest)
- Check Anthropic's status page

### 401 Unauthorized Error

Your API key is invalid:
1. Go to https://console.anthropic.com/account/keys
2. Verify the key is active (not revoked)
3. Copy the full key (including `sk-ant-` prefix)
4. Update `.env` with the correct key

## Integration with RAG Router

The RAG Router (`tools/rag_router/`) automatically supports Claude API:

```python
# In rag_router service code
result = await proxy.query(
    model="claude-3-5-sonnet-20241022",
    prompt=user_query,
    backend="anthropic",
    use_rag=True  # Inject knowledge base context
)
```

## Integration with Multi-Agent Framework

Use Claude in your Orion agents:

```python
# In your agent code
from tools.multi-agent-framework.hybrid_model_proxy import HybridModelProxy

proxy = HybridModelProxy()
response = await proxy.query(
    model="claude-3-5-sonnet-20241022",
    prompt="Task description",
    backend="anthropic"
)
```

## Next Steps

1. ✅ Get your API key from Anthropic console
2. ✅ Add to `.env` file
3. ✅ Run health check: `python hybrid_model_proxy.py`
4. ✅ Test with sample code
5. ✅ Monitor usage at console.anthropic.com

---

**Need Help?**
- Anthropic Docs: https://docs.anthropic.com/
- Status Page: https://status.anthropic.com/
- API Key Setup: https://console.anthropic.com/account/keys
