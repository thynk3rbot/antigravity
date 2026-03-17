import asyncio
import httpx
import sys

# Antigravity Global Environment Map
ENVIRONMENTS = {
    "LORALINK": "http://localhost:8000",
    "NUTRICYCLE": "http://localhost:8100",
    "RAG_ROUTER": "http://localhost:8200", 
    "VIAI_CLUB": "http://localhost:8010"
}

async def check_endpoint(name, url, path="/", expected_text=None):
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(f"{url}{path}")
            if resp.status_code == 200:
                if expected_text and expected_text not in resp.text:
                    print(f"[-] {name:15} | WARNING: Content mismatch at {path}")
                    return False
                print(f"[+] {name:15} | OK ({url}{path})")
                return True
            else:
                print(f"[-] {name:15} | FAILED: HTTP {resp.status_code}")
                return False
    except Exception as e:
        print(f"[X] {name:15} | UNREACHABLE: {e}")
        return False

async def run_harness():
    print("="*60)
    print(" ANTIGRAVITY UNIVERSAL TEST HARNESS")
    print("="*60 + "\n")
    
    tasks = [
        # 1. LoRaLink Suite
        check_endpoint("LoRaLink", ENVIRONMENTS["LORALINK"], "/", "LoRaLink"),
        check_endpoint("LoRa API", ENVIRONMENTS["LORALINK"], "/api/nodes"),
        
        # 2. NutriCalc
        check_endpoint("NutriCalc", ENVIRONMENTS["NUTRICYCLE"], "/", "NutriCalc"),
        
        # 3. Rag-Router
        check_endpoint("Rag-Router", ENVIRONMENTS["RAG_ROUTER"], "/health"),
        
        # 4. viai.club
        check_endpoint("viai.club", ENVIRONMENTS["VIAI_CLUB"], "/", "viai.club"),
    ]
    
    results = await asyncio.gather(*tasks)
    
    print("\n" + "="*60)
    if all(results):
        print(" ✨ ALL SYSTEMS OPERATIONAL - MISSION GO")
    else:
        print(" ⚠️ SYSTEM DEGRADATION DETECTED - CHECK LOGS")
    print("="*60)

if __name__ == "__main__":
    asyncio.run(run_harness())
