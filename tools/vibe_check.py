import asyncio
import httpx
import sys
import os

# -- Vibe Test Configuration --
ENVIRONMENTS = {
    "Magic": {"url": "http://127.0.0.1:8000", "path": "/api/nodes"},
    "NutriCalc": {"url": "http://127.0.0.1:8100", "path": "/"},
    "Rag-Router": {"url": "http://127.0.0.1:8200", "path": "/health"},
    "viai.club": {"url": "http://127.0.0.1:8010", "path": "/"}
}

async def check_vibe(name, config):
    url = f"{config['url']}{config['path']}"
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            resp = await client.get(url)
            if resp.status_code == 200:
                print(f"  [+] {name:15} | ✅ RESONANT  ({url})")
                return True
            else:
                print(f"  [-] {name:15} | ❌ DISCORDANT (HTTP {resp.status_code})")
                return False
    except Exception as e:
        print(f"  [X] {name:15} | 🌑 SILENT     ({type(e).__name__})")
        return False

async def run_vibe_test():
    print("\n" + "≃"*60)
    print(" 🛸 ANTIGRAVITY UNIVERSAL VIBE CHECK (v2.0)")
    print(" " + "≃"*60 + "\n")
    
    tasks = [check_vibe(name, cfg) for name, cfg in ENVIRONMENTS.items()]
    results = await asyncio.gather(*tasks)
    
    print("\n" + "="*60)
    if all(results):
        print(" ✨ HARMONY ACHIEVED - ALL NODES OPERATIONAL")
    else:
        print(" ⚠️ INTERFERENCE DETECTED - RECALIBRATING...")
    print("="*60 + "\n")

if __name__ == "__main__":
    asyncio.run(run_vibe_test())
