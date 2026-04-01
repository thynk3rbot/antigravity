import os
import json
import requests
import time

# --- Configuration ---
DIFY_API_BASE = "http://localhost:8400/v1"
DATASET_ID = "c6e1fc7b-be33-44ab-aa05-ca75eb2009c8"
API_KEY = "dataset-Uu2Bx26RGsO3SjFQcW8dcmGx"

KNOWLEDGE_PATHS = [
    r"C:\Users\spw1\Documents\Code\VIAI.CLUB\Official_Knowledge"
]

def upload_document(file_path):
    url = f"{DIFY_API_BASE}/datasets/{DATASET_ID}/document/create-by-file"
    headers = {"Authorization": f"Bearer {API_KEY}"}
    
    # Dify processing rules
    data_payload = {
        "indexing_technique": "high_quality",
        "process_rule": {
            "mode": "automatic"
        }
    }
    
    file_name = os.path.basename(file_path)
    print(f"[INFO] Uploading: {file_name}...", end="", flush=True)
    
    try:
        with open(file_path, "rb") as f:
            files = {
                'file': (file_name, f),
                'data': (None, json.dumps(data_payload), 'application/json')
            }
            response = requests.post(url, headers=headers, files=files, timeout=120)
            
            if response.status_code in [200, 201]:
                print(" ✅ Success")
                return True
            else:
                print(f" ❌ Failed ({response.status_code})")
                print(f"      Response: {response.text}")
                return False
    except Exception as e:
        print(f" ❌ Error: {e}")
        return False

def main():
    if API_KEY == "PASTE_YOUR_DATASET_API_KEY_HERE":
        print("[CRITICAL] Please set your DIFY_API_KEY in the script first!")
        return

    all_files = []
    for path in KNOWLEDGE_PATHS:
        if os.path.isdir(path):
            for f in os.listdir(path):
                if f.lower().endswith(('.pdf', '.docx')):
                    all_files.append(os.path.join(path, f))
    
    print(f"--- VIAI Knowledge Ingestion ---")
    print(f"Found {len(all_files)} documents to upload.\n")
    
    success_count = 0
    for file_path in all_files:
        if upload_document(file_path):
            success_count += 1
        time.sleep(1) # Small delay for the API
        
    print(f"\n[DONE] Successfully uploaded {success_count} / {len(all_files)} documents.")
    print("Go to your Dify dashboard to monitor the progress of the 'Indexing' phase.")

if __name__ == "__main__":
    main()
