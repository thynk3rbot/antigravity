# Magic — Quick Start

Run the complete Magic platform with a single command.

## One-Click Launch

Double-click **`Start_Magic.bat`** from the repo root. This starts:

| Service | Port | Description |
| :--- | :--- | :--- |
| **MQTT Broker** | `1883` | Mosquitto (local, no Docker required) |
| **Magic Daemon** | `8001` | Core REST API — fleet, mesh, service manager |
| **Fleet Dashboard** | `8000` | Web UI for device management and OTA |
| **Magic Messenger** | `8400` | PWA mesh chat — install on phone via browser |
| **AI Assistant** | `8300` | Local AI assistant with mesh context |

## Background Mode (Headless)

For a fully supervised background stack with auto-restart on crash:

```powershell
python tools/start_bg_services.py
# Stop with:
python tools/start_bg_services.py stop
```

All logs written to `logs/` in the repo root.

## Requirements

- Python 3.10+ in PATH
- [Mosquitto](https://mosquitto.org/download/) installed (`winget install mosquitto`)
- Run `pip install -r daemon/requirements.txt` once

## Quick Links (once running)

- **Fleet Dashboard** → http://localhost:8000
- **Daemon API** → http://localhost:8001/docs
- **Magic Messenger** → http://localhost:8400 (add to home screen on phone)

## Firmware

Flash a device: `pio run -t upload -e heltec_v4` (version auto-increments).
