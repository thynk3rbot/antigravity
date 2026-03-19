import requests
import json

url = "http://localhost:8000/api/test/regression"
payload = {"target": "Slave"}

try:
    response = requests.post(url, json=payload, timeout=2)
    print(f"Status: {response.status_code}")
    print(f"Body: {response.text}")
except Exception as e:
    print(f"Request failed: {e}")
