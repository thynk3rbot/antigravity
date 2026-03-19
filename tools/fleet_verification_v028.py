import httpx
import asyncio
import json
import sys

# Fleet configuration
TARGETS = ["172.16.0.26", "172.16.0.27", "172.16.0.28", "172.16.0.29"]
EXPECTED_VERSION = "v0.2.8"

async def verify_node(ip):
    url = f"http://{ip}/api/status"
    print(f"[*] Checking {ip}...")
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(url)
            if resp.status_code == 200:
                data = resp.json()
                ver = data.get("version", "unknown")
                hw = data.get("hw_var", "unknown")
                gps_en = data.get("gps_en", False)
                gps_chars = data.get("gps_chars", 0)
                
                status = "PASS" if ver == EXPECTED_VERSION else "FAIL (Version Mismatch)"
                print(f"  [+] {ip}: Version={ver}, HW_Var={hw}, GPS={gps_en}, Chars={gps_chars} -> {status}")
                return True
            else:
                print(f"  [-] {ip}: HTTP {resp.status_code}")
    except Exception as e:
        print(f"  [-] {ip}: Unreachable ({type(e).__name__})")
    return False

async def main():
    print(f"--- LoRaLink Fleet Verification: {EXPECTED_VERSION} ---")
    results = await asyncio.gather(*(verify_node(ip) for ip in TARGETS))
    success_count = sum(1 for r in results if r)
    print(f"\nSummary: {success_count}/{len(TARGETS)} nodes verified.")

if __name__ == "__main__":
    asyncio.run(main())
