import requests
import socket
import time
from concurrent.futures import ThreadPoolExecutor

# Predefined fleet IDs based on user's mDNS hostnames
# loralink-26, loralink-27, loralink-28, loralink-29, loralink-30
FLEET_HOSTS = [f"loralink-{i}.local" for i in range(26, 31)]

def check_node(host):
    url = f"http://{host}/api/status"
    try:
        start_time = time.time()
        response = requests.get(url, timeout=3)
        latency = int((time.time() - start_time) * 1000)
        
        if response.status_code == 200:
            data = response.json()
            return {
                "host": host,
                "status": "ONLINE",
                "id": data.get("id", "??"),
                "version": data.get("version", "??"),
                "battery": data.get("bat", "??"),
                "rssi": data.get("rssi", "??"),
                "uptime": data.get("uptime", "??"),
                "latency": f"{latency}ms"
            }
    except Exception:
        pass
    
    return {"host": host, "status": "OFFLINE"}

def report():
    print("\n" + "="*80)
    print(f"{'LoRaLink Multi-Location Fleet Health Report':^80}")
    print("="*80)
    print(f"{'Hostname':<18} | {'Status':<8} | {'ID':<6} | {'Ver':<6} | {'Bat':<6} | {'RSSI':<6} | {'Latency'}")
    print("-" * 80)
    
    with ThreadPoolExecutor(max_workers=5) as executor:
        results = list(executor.map(check_node, FLEET_HOSTS))
        
    for r in results:
        if r["status"] == "ONLINE":
            print(f"{r['host']:<18} | \033[92m{r['status']:<8}\033[0m | {r['id']:<6} | {r['version']:<6} | {r['battery']:<6} | {r['rssi']:<6} | {r['latency']}")
        else:
            print(f"{r['host']:<18} | \033[91m{r['status']:<8}\033[0m | {'--':<6} | {'--':<6} | {'--':<6} | {'--':<6} | --")
    
    print("="*80 + "\n")

if __name__ == "__main__":
    report()
