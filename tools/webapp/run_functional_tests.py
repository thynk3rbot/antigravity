import asyncio
import httpx
import sys

BASE_URL = "http://localhost:8000"

async def test_webapp_functional():
    print(f"🚀 Starting Magic Webapp Functional Tests on {BASE_URL}...")
    
    success_all = True
    async with httpx.AsyncClient(timeout=10.0) as client:
        # 1. Check Root Page
        try:
            resp = await client.get(BASE_URL)
            assert resp.status_code == 200
            assert "Magic" in resp.text
            print("✅ Root Page: OK")
        except Exception as e:
            print(f"❌ Root Page Failed: {e}")
            success_all = False

        # 2. Check Board Registry API
        try:
            resp = await client.get(f"{BASE_URL}/api/boards/heltec_v3")
            if resp.status_code == 200:
                data = resp.json()
                assert data["name"] == "Heltec WiFi LoRa 32 V3"
                print("✅ Board API: OK")
            else:
                print(f"❌ Board API Failed: Status {resp.status_code}")
                success_all = False
        except Exception as e:
            print(f"❌ Board API Exception: {e}")
            success_all = False

        # 3. Check Filesystem API
        try:
            resp = await client.get(f"{BASE_URL}/api/files/list")
            # We accept 200 (local) or 502 (device offline proxy) as "endpoint exists"
            if resp.status_code in [200, 502]:
                print(f"✅ Filesystem API Endpoint: OK (Status {resp.status_code})")
            else:
                print(f"❌ Filesystem API Failed: Status {resp.status_code}")
                success_all = False
        except Exception as e:
            print(f"❌ Filesystem API Exception: {e}")
            success_all = False

        # 4. Check Nodes API
        try:
            resp = await client.get(f"{BASE_URL}/api/nodes")
            if resp.status_code == 200:
                print("✅ Nodes API: OK")
            else:
                print(f"❌ Nodes API Failed: Status {resp.status_code}")
                success_all = False
        except Exception as e:
            print(f"❌ Nodes API Exception: {e}")
            success_all = False

        # 5. Check Command API
        try:
            resp = await client.post(f"{BASE_URL}/api/cmd", json={"cmd": "PING"})
            if resp.status_code == 200:
                print("✅ Command API (PING): OK")
            else:
                print(f"❌ Command API Failed: Status {resp.status_code}")
                success_all = False
        except Exception as e:
            print(f"❌ Command API Exception: {e}")
            success_all = False

    if success_all:
        print("\n✨ All core functional API tests passed!")
    else:
        print("\n⚠️ Some tests failed. Check output above.")
    
    return success_all

if __name__ == "__main__":
    success = asyncio.run(test_webapp_functional())
    if not success:
        sys.exit(1)
