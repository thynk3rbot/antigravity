#!/usr/bin/env python3
"""
Magic PC AI Daemon
Listens on the designated Serial port for incoming `AI_QUERY:` requests 
originating from the mesh network, routes them to the local Ollama instance, 
and pipes the response back across the mesh.
"""

import sys
import time
import requests
import argparse
try:
    import serial
except ImportError:
    print("Error: pyserial not found. Please install via: pip install pyserial")
    sys.exit(1)

OLLAMA_API = "http://localhost:11434/api/generate"
# Default to gemma3 or qwen2.5-coder based on user workstation setup
DEFAULT_MODEL = "qwen2.5-coder:14b"
MAX_RESPONSE_LEN = 200 # Prevent LoRa payload overflow

def get_config_from_device(ser, key):
    """Query the device for a config value via Serial."""
    ser.write(f"CONFIG GET {key}\n".encode('utf-8'))
    start_time = time.time()
    while time.time() - start_time < 2: # 2 second timeout
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if f"CONFIG: {key.lower()} =" in line.lower():
                return line.split('=')[-1].strip()
    return None

def query_local_ai(prompt, model):
    print(f"\n[AI] Prompt received: '{prompt}'")
    print(f"[AI] Generating response via {model}...")
    
    try:
        response = requests.post(OLLAMA_API, json={
            "model": model,
            "prompt": prompt,
            "stream": False
        }, timeout=120)
        
        if response.status_code == 200:
            result = response.json().get('response', '').strip()
            # Magic expects single-line commands. Strip newlines to play nice with Serial routing.
            result = result.replace('\n', ' ').replace('\r', '')
            
            # Truncate if it exceeds reasonable packet sizes for LoRa/ESP-NOW
            if len(result) > MAX_RESPONSE_LEN:
                result = result[:MAX_RESPONSE_LEN-3] + "..."
                
            return result
        else:
            return f"API_ERR_{response.status_code}"
            
    except requests.exceptions.ConnectionError:
        return "ERR: Ollama not running on localhost:11434"
    except Exception as e:
        return f"ERR: {str(e)[:40]}"

def start_daemon(port, baud, model):
    print("==========================================")
    print("   Magic Local AI Workstation Daemon   ")
    print("==========================================")
    print(f"[*] Target Port : {port}")
    print(f"[*] Baud Rate   : {baud}")
    print(f"[*] LLM Target  : {model}")
    print("[*] Connecting...")
    
    try:
        ser = serial.Serial(port, baud, timeout=1)
        print(f"[+] Connected to Gateway on {port}")
    except Exception as e:
        print(f"[!] Critical: Could not open {port}: {e}")
        print("    Check if Arduino IDE or Serial Monitor is blocking the port.")
        sys.exit(1)

    # Prime the board to ensure serial streaming is enabled
    print("[*] Priming Gateway Stream output...")
    ser.write(b"STREAM ON\n")
    time.sleep(0.5)

    # Attempt to sync configuration from device
    print("[*] Syncing AI config from device...")
    dev_provider = get_config_from_device(ser, "AI_PROVIDER")
    dev_model = get_config_from_device(ser, "AI_MODEL")
    
    if dev_provider:
        print(f"[+] Device Provider: {dev_provider}")
    if dev_model and dev_model != "unknown":
        print(f"[+] Device Model (Overriding): {dev_model}")
        model = dev_model
    
    if dev_provider and dev_provider.lower() != "ollama":
        print(f"[!] Warning: Device is set to provider '{dev_provider}', but this daemon only supports Ollama.")

    print(f"[*] Daemon listening for 'AI_QUERY:' payloads via {model}...\n")
    
    while True:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                # Echo diagnostic logs if they are important, or just let them pass
                if "AI_QUERY:" in line:
                    parts = line.split("AI_QUERY:", 1)
                    if len(parts) > 1:
                        prompt = parts[1].strip()
                        
                        # Execute the query
                        ai_response = query_local_ai(prompt, model)
                        
                        # Construct the mesh broadcast command: ALL <msg>
                        print(f"[*] AI Response : {ai_response}")
                        cmd_to_send = f"ALL AI: {ai_response}\n"
                        
                        # Transmit to Gateway
                        ser.write(cmd_to_send.encode('utf-8'))
                        print("[+] Response dispatched to mesh.\n")
                        
        except KeyboardInterrupt:
            print("\n[!] Ctrl+C detected. Shutting down daemon.")
            ser.close()
            break
        except Exception as e:
            print(f"[!] Serial Error: {e}")
            time.sleep(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Magic to Ollama Bridge Daemon")
    parser.add_argument("-p", "--port", type=str, default="COM3", help="Serial COM port attached to the Magic Master/Gateway")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument("-m", "--model", type=str, default=DEFAULT_MODEL, help="Local Ollama model to query (default qwen2.5-coder:14b)")
    
    args = parser.parse_args()
    start_daemon(args.port, args.baud, args.model)
