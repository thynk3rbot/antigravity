import urllib.request
import json
import zipfile
import os
import shutil

url = 'https://api.github.com/repos/meshtastic/firmware/releases/latest'
response = urllib.request.urlopen(url)
data = json.loads(response.read())

target_url = None
for asset in data['assets']:
    if 'firmware-esp32s3' in asset['name'] and 'debug' not in asset['name']:
        target_url = asset['browser_download_url']
        break

if target_url is None:
    print("Could not find esp32s3 firmware zip")
    exit(1)

print(f"Downloading {target_url}...")
urllib.request.urlretrieve(target_url, 'meshtastic_firmware.zip')
print("Download complete. Extracting...")

os.makedirs('meshtastic_latest', exist_ok=True)
with zipfile.ZipFile('meshtastic_firmware.zip', 'r') as zip_ref:
    zip_ref.extractall('meshtastic_latest')

print("Extraction complete. Finding T-TWR binaries...")
twr_files = []
for root, dirs, files in os.walk('meshtastic_latest'):
    for file in files:
        if 't-twr' in file.lower() or 'ttwr' in file.lower():
            twr_files.append(os.path.join(root, file))
            print(f"Found: {file}")

print("Done.")
