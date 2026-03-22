import time
import sys
import json

def run_fake_sim():
    print("=== LoRaLink Fake Simulator (Test Script) ===")
    sys.stdout.flush()
    
    last_query = 0
    
    try:
        while True:
            # Output dummy status JSON
            now_ms = int(time.time() * 1000)
            status = {
                "uptime": now_ms,
                "pins": [
                    {"id": 25, "val": 0},
                    {"id": 34, "val": 2000 + (now_ms // 1000 % 100)}
                ]
            }
            print(json.dumps(status))
            sys.stdout.flush()
            
            # Periodic AI_QUERY every 15 seconds
            if time.time() - last_query >= 15:
                print("AI_QUERY: Hello from the fake simulator! Is everything OK?")
                sys.stdout.flush()
                last_query = time.time()
                
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting fake sim.")

if __name__ == "__main__":
    run_fake_sim()
