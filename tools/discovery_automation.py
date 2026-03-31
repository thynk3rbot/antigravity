import asyncio
import httpx
import sys
import time
import json

BASE_URL = "http://localhost:8000"

async def discovery_and_reboot():
    print("="*60)
    print(" ANTIGRAVITY AUTOMATION: BROAD TRANSPORT SWEEP & REBOOT")
    print("="*60)
    
    async with httpx.AsyncClient(timeout=30.0) as client:
        # 1. Trigger Broad Discovery
        print("\n[1/5] Querying Discovery Snapshot (mDNS, BLE, Serial)...")
        await asyncio.sleep(5)  # Allow background threads to spin
        
        try:
            resp = await client.get(f"{BASE_URL}/api/discovery")
            snapshot = resp.json()
            
            print(f"    - mDNS Hosts  : {len(snapshot.get('mdns', []))}")
            print(f"    - BLE Assets  : {len(snapshot.get('ble', []))}")
            print(f"    - Serial Ports: {len(snapshot.get('serial', []))}")
            
            # 2. Auto-Registration Logic
            print("\n[2/5] Auto-Registering Targeted Peers...")
            
            # Check mDNS for Peer1
            for dev in snapshot.get('mdns', []):
                if "Peer1" in dev['name'] or "magic" in dev['name'].lower():
                    print(f"    [+] Registering WiFi Peer: {dev['name']} @ {dev['ip']}")
                    await client.post(f"{BASE_URL}/api/discovery/register", json={
                        "name": dev['name'], "type": "wifi", "address": dev['ip']
                    })
            
            for port in snapshot.get('serial', []):
                # Only register if it matches our filtered list from backend
                print(f"    [+] Registering Serial Peer on {port}")
                await client.post(f"{BASE_URL}/api/discovery/register", json={
                    "name": f"Peer-USB-{port.replace('COM','')}", "type": "serial", "address": port
                })
        except Exception as e:
            print(f"[-] Discovery query failed: {e}")

        # 3. Fetch Final Registry
        resp = await client.get(f"{BASE_URL}/api/nodes")
        data = resp.json()
        registered_nodes = data if isinstance(data, list) else data.get("nodes", [])

        # 4. Reboot Cycle
        targets = [n for n in registered_nodes if isinstance(n, dict) and "Peer" in n.get('name', '')]
        print(f"\n[3/5] Initiating Reboot Sequence for {len(targets)} targets...")
        
        for node in targets:
            print(f"\n[REBOOT] {node['name']} ({node['type']})...")
            try:
                # Send REBOOT via universal command API
                cmd_resp = await client.post(f"{BASE_URL}/api/cmd", json={
                    "cmd": "REBOOT", "node_id": node['id']
                })
                print(f"    [OK] Command state: {cmd_resp.status_code}")
            except Exception as e:
                print(f"    [X] Failed: {e}")

        # 5. Recovery Analysis
        print("\n[4/5] Monitoring Recovery (45s window)...")
        start_time = time.time()
        recovered = set()
        while time.time() - start_time < 45:
            for node in targets:
                if node['id'] in recovered: continue
                try:
                    p_resp = await client.post(f"{BASE_URL}/api/cmd", json={
                        "cmd": "VERSION", "node_id": node['id']
                    })
                    if p_resp.status_code == 200:
                        print(f"    [+] {node['name']} RECOVERED. Trace active.")
                        recovered.add(node['id'])
                except: pass
            if len(recovered) == len(targets): break
            await asyncio.sleep(3)

        print("\n[5/5] Final Status Report")
        print("-" * 30)
        for node in targets:
            status = "ONLINE" if node['id'] in recovered else "TIMEOUT"
            print(f" {node['name']:15} | {node['type']:8} | {status}")
        print("-" * 30)

    print("\n" + "="*60)
    print(f" AUTOMATION COMPLETE: {len(recovered)}/{len(targets)} Verified.")
    print("="*60)

if __name__ == "__main__":
    asyncio.run(discovery_and_reboot())
