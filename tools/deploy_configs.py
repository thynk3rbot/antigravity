#!/usr/bin/env python3
"""
Two-Device Configuration Deployment Script
Deploys different configs to Master and Slave devices
- Master: generic config (heltec_v3_generic.json)
- Slave: farm automation config (heltec_v3_farm_automation.json)
"""

import json
import aiohttp
import asyncio
import sys
from pathlib import Path

BASE_DIR = Path(__file__).parent.parent
CONFIGS_DIR = BASE_DIR / "tools/webapp/configs"

WEBAPP_BASE = "http://localhost:8000"

DEVICES = [
    {
        "name": "Master",
        "id": "3c11206a-2909-47f2-b357-ee0937b6b396",
        "config_file": "heltec_v3_generic.json",
        "expected_name": "LoRaLink-TestUnit-V3",
    },
    {
        "name": "Slave",
        "id": "683e5fef-5377-4c7a-9e01-9fffc6e41ef2",
        "config_file": "heltec_v3_farm_automation.json",
        "expected_name": "FarmGateway-North",
    },
]


async def deploy_config(session, device):
    """Deploy config to a single device"""
    config_path = CONFIGS_DIR / device["config_file"]
    if not config_path.exists():
        return {"ok": False, "error": f"Config file not found: {config_path}"}

    try:
        # Step 1: Connect to the device via webapp
        print(f"\n[{device['name']}] Connecting...")
        connect_url = f"{WEBAPP_BASE}/api/nodes/{device['id']}/connect"
        async with session.post(connect_url, timeout=aiohttp.ClientTimeout(total=10)) as resp:
            if resp.status != 200:
                return {"ok": False, "error": f"Failed to connect: HTTP {resp.status}"}

        await asyncio.sleep(1)

        # Step 2: Deploy config via webapp proxy
        with open(config_path) as f:
            config = json.load(f)

        print(f"  > Deploying {device['config_file']}...")
        print(f"  > Target device name: {device['expected_name']}")

        url = f"{WEBAPP_BASE}/api/device/config/apply"
        async with session.post(url, json=config, timeout=aiohttp.ClientTimeout(total=30)) as resp:
            if resp.status == 200:
                print(f"  [OK] Config applied successfully")

                # Wait for reboot and verify
                await asyncio.sleep(3)
                return await verify_config(session, device)
            else:
                text = await resp.text()
                return {"ok": False, "error": f"HTTP {resp.status}: {text}"}
    except asyncio.TimeoutError:
        return {"ok": False, "error": "Request timeout (device may be rebooting)"}
    except Exception as e:
        return {"ok": False, "error": str(e)}


async def verify_config(session, device):
    """Verify config was applied by exporting and checking device name"""
    try:
        url = f"{WEBAPP_BASE}/api/device/config"
        async with session.get(url, timeout=aiohttp.ClientTimeout(total=30)) as resp:
            if resp.status == 200:
                exported = await resp.json()
                actual_name = exported.get("settings", {}).get("dev_name", "UNKNOWN")

                if actual_name == device["expected_name"]:
                    print(f"  [OK] Verified: device name is '{actual_name}'")
                    return {"ok": True, "device_name": actual_name}
                else:
                    return {
                        "ok": False,
                        "error": f"Name mismatch: expected '{device['expected_name']}', got '{actual_name}'",
                    }
            else:
                return {"ok": False, "error": f"Failed to export config: HTTP {resp.status}"}
    except asyncio.TimeoutError:
        return {"ok": False, "error": "Verification timeout (device still rebooting)"}
    except Exception as e:
        return {"ok": False, "error": f"Verification failed: {str(e)}"}


async def main():
    """Deploy to both devices sequentially"""
    print("=" * 70)
    print("TWO-DEVICE CONFIGURATION DEPLOYMENT")
    print("=" * 70)

    results = []

    async with aiohttp.ClientSession() as session:
        for device in DEVICES:
            result = await deploy_config(session, device)
            result["device"] = device["name"]
            results.append(result)

            if not result["ok"]:
                print(f"  [FAIL] FAILED: {result.get('error', 'Unknown error')}")

            # Wait between devices
            await asyncio.sleep(2)

    # Summary
    print("\n" + "=" * 70)
    print("DEPLOYMENT SUMMARY")
    print("=" * 70)

    for result in results:
        status = "[OK] SUCCESS" if result["ok"] else "[FAIL] FAILED"
        device_name = result.get("device_name", "N/A")
        print(f"\n{result['device']}: {status}")
        if result["ok"]:
            print(f"  Device name: {device_name}")
        else:
            print(f"  Error: {result.get('error', 'Unknown')}")

    all_ok = all(r["ok"] for r in results)
    print("\n" + "=" * 70)
    if all_ok:
        print("[OK] All devices deployed successfully!")
        print("\nNext steps:")
        print("1. Open webapp: python tools/webapp/server.py --ip 172.16.0.26")
        print("2. Load task schedules:")
        print("   - Master: import tasks_generic_toggle.json")
        print("   - Slave: import tasks_farm_scenario.json")
    else:
        print("[FAIL] Some devices failed. Check errors above.")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
