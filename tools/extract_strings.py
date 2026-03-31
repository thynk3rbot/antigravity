import sys
import re

filename = 'lilygo_fw_dump.bin'
try:
    with open(filename, 'rb') as f:
        data = f.read()

    # Extract all ASCII strings of length >= 6
    strings = re.findall(b'[ -~]{6,}', data)
    strings = [s.decode('utf-8') for s in strings]

    keywords = ['Mesh', 'password', 'Lily', 'SSID', 'http', 'mqtt']
    found = []

    for s in strings:
        if any(k.lower() in s.lower() for k in keywords):
            if len(s) < 100:
                found.append(s)

    # Print a sample of interesting strings
    found = list(set(found)) # deduplicate
    print(f"Found {len(found)} interesting strings. Sample:")
    for string in found[:20]:
        print(f" - {string}")

except Exception as e:
    print(f"Error: {e}")
