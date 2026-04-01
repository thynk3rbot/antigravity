# Test Data Pump — AG Implementation Prompt

**Date:** 2026-04-01
**Prerequisites:** `git pull origin main`
**Goal:** Create a test data pump plugin that publishes fake device telemetry to MQTT, indistinguishable from real firmware.

---

## PROMPT START

You are building a **test data pump** that simulates a fleet of Magic devices by publishing MQTT messages. The daemon, dashboard, LVC service, and all plugins receive this data exactly as if it came from real hardware. This is essential for development without physical devices and for customer demos.

### Existing Code to Reference (DO NOT copy blindly — use as reference)

We already have two spoofing/simulation files in the repo. **Read these first** for patterns and lessons:

1. **`tools/testing/mqtt_spoof.py`** — MQTT command spoofer with paho v2 API (`CallbackAPIVersion.VERSION2`), async connect, clean callback structure. Use this as reference for the MQTT client setup pattern.
2. **`daemon/src/transmitter.py`** — Simple telemetry generator that publishes fake battery/RSSI/uptime to MQTT. Uses the **old** `MagicCache/` topic prefix (not the firmware contract). The test pump must use `magic/{node_id}/telemetry` instead.

**What to take from them:** paho-mqtt connection pattern, callback structure, publish loop.
**What NOT to take:** the `MagicCache/` topic prefix (wrong), the v1 paho Client API (use v2), the async wrapper (pump should be synchronous/threaded, not asyncio).

### What You're Building

A plugin directory at `plugins/test-pump/` with:

```
plugins/test-pump/
├── plugin.json           ← self-describing manifest
├── pump.py               ← main entry point
├── scenarios/
│   ├── healthy_fleet.json
│   └── low_battery.json
├── requirements.txt
└── .env.example
```

### plugin.json

```json
{
  "$schema": "magic-plugin-v1",
  "name": "test-pump",
  "display_name": "Test Data Pump",
  "description": "Simulates Magic device fleet telemetry for development and demos",
  "version": "1.0.0",

  "run": {
    "cmd": "python pump.py",
    "cwd": ".",
    "env_file": ".env",
    "language": "python",
    "requirements": "requirements.txt"
  },

  "port": null,
  "health": null,

  "infrastructure": {
    "requires": ["mqtt"],
    "docker_compose": null
  },

  "auto_start": false,
  "restart_policy": "never",

  "menu": {
    "group": "Development",
    "icon": "🧪",
    "actions": [
      {"label": "Start Pump", "type": "action", "value": "start"},
      {"label": "Stop Pump", "type": "action", "value": "stop"},
      {"label": "View Logs", "type": "action", "value": "logs"}
    ]
  }
}
```

### pump.py — Core Implementation

The pump reads a scenario file and publishes MQTT messages on a timer.

```python
"""
Magic Test Data Pump — Simulates device fleet telemetry.
Publishes to MQTT using the same topic contract as real firmware.

Usage:
    python pump.py [--scenario healthy_fleet] [--interval 5] [--broker localhost:1883]
"""

import argparse
import json
import logging
import os
import random
import time
from pathlib import Path

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("test-pump")

MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
SCENARIO_DIR = Path(__file__).parent / "scenarios"
DEFAULT_INTERVAL = float(os.getenv("PUMP_INTERVAL", "5"))
```

**What `pump.py` must do:**

1. **Load a scenario file** from `scenarios/` — a JSON array of simulated devices
2. **Connect to MQTT** at the configured broker
3. **Loop forever**, publishing one telemetry update per device per interval
4. **Vary values realistically** — battery drains slowly, RSSI fluctuates ±5, uptime increments
5. **Publish to the correct topics** matching the firmware MQTT contract:
   - `magic/{node_id}/telemetry` → `{"uptime_ms": ..., "battery_mv": ..., "battery_pct": ..., "rssi": ..., "neighbors": [...]}`
   - `magic/{node_id}/status` → `"ONLINE"`
6. **Support graceful shutdown** via SIGINT/SIGTERM
7. **Log each publish** so the user can see the pump is working

### Scenario File Format

`scenarios/healthy_fleet.json`:
```json
{
  "name": "Healthy Fleet",
  "description": "3 devices with normal telemetry, stable battery, good signal",
  "devices": [
    {
      "node_id": "Magic-A3F2",
      "battery_mv_start": 4100,
      "battery_drain_mv_per_hour": 10,
      "rssi_center": -42,
      "rssi_jitter": 5,
      "neighbors": ["Magic-B1E7", "Magic-C4D9"],
      "relay_1": false,
      "relay_2": false,
      "gps": {"lat": 40.7128, "lon": -74.0060, "alt": 10.0}
    },
    {
      "node_id": "Magic-B1E7",
      "battery_mv_start": 3800,
      "battery_drain_mv_per_hour": 15,
      "rssi_center": -58,
      "rssi_jitter": 8,
      "neighbors": ["Magic-A3F2"],
      "relay_1": true,
      "relay_2": false,
      "gps": {"lat": 40.7580, "lon": -73.9855, "alt": 25.0}
    },
    {
      "node_id": "Magic-C4D9",
      "battery_mv_start": 3600,
      "battery_drain_mv_per_hour": 20,
      "rssi_center": -65,
      "rssi_jitter": 10,
      "neighbors": ["Magic-A3F2"],
      "relay_1": false,
      "relay_2": true,
      "gps": null
    }
  ]
}
```

`scenarios/low_battery.json`:
```json
{
  "name": "Low Battery Alert",
  "description": "One device draining fast — triggers battery warnings",
  "devices": [
    {
      "node_id": "Magic-DEAD",
      "battery_mv_start": 3300,
      "battery_drain_mv_per_hour": 100,
      "rssi_center": -75,
      "rssi_jitter": 15,
      "neighbors": [],
      "relay_1": false,
      "relay_2": false,
      "gps": null
    }
  ]
}
```

### Telemetry Generation Logic

For each device, each tick:

```python
def generate_telemetry(device_state: dict, elapsed_s: float) -> dict:
    """Generate one telemetry payload for a simulated device."""
    # Battery drains linearly
    drain = device_state["battery_drain_mv_per_hour"] * (elapsed_s / 3600)
    battery_mv = max(3000, device_state["battery_mv_start"] - drain)

    # Battery percentage: linear map 4200mv=100%, 3000mv=0%
    battery_pct = max(0, min(100, int((battery_mv - 3000) / 12)))

    # RSSI jitters around center
    rssi = device_state["rssi_center"] + random.randint(
        -device_state["rssi_jitter"],
        device_state["rssi_jitter"]
    )

    # Uptime increments
    uptime_ms = int(elapsed_s * 1000)

    payload = {
        "uptime_ms": uptime_ms,
        "battery_mv": int(battery_mv),
        "battery_pct": battery_pct,
        "rssi": rssi,
        "neighbors": device_state.get("neighbors", []),
        "relay_1": device_state.get("relay_1", False),
        "relay_2": device_state.get("relay_2", False),
        "free_heap": random.randint(180000, 220000),
        "version": "0.0.22V4",
    }

    if device_state.get("gps"):
        # Slight GPS drift for realism
        gps = device_state["gps"]
        payload["gps"] = {
            "lat": gps["lat"] + random.uniform(-0.0001, 0.0001),
            "lon": gps["lon"] + random.uniform(-0.0001, 0.0001),
            "alt": gps["alt"] + random.uniform(-0.5, 0.5),
        }

    return payload
```

### .env.example

```
MQTT_BROKER=localhost
MQTT_PORT=1883
PUMP_INTERVAL=5
PUMP_SCENARIO=healthy_fleet
```

### requirements.txt

```
paho-mqtt>=2.0
python-dotenv
```

### ESP32/Safety Checklist

- [ ] **Topics match firmware contract exactly:** `magic/{node_id}/telemetry` and `magic/{node_id}/status`
- [ ] **Payload JSON matches firmware STATUS output:** same field names, same types
- [ ] **Battery values are realistic:** 3000-4200 mV range, percentage 0-100
- [ ] **RSSI values are realistic:** -30 to -90 dBm range
- [ ] **Uptime increments monotonically** — never resets unless scenario says "reboot"
- [ ] **Graceful shutdown** — disconnects MQTT client on SIGINT
- [ ] **No hardcoded broker address** — reads from .env or CLI args
- [ ] **Scenario files are valid JSON** — parseable without errors

### What NOT To Do

1. **Do NOT use the MxWire binary format** — the pump speaks the same plaintext JSON that current firmware publishes over MQTT. MxWire integration is a Phase 4 concern.
2. **Do NOT modify any existing file** — this is a new plugin directory, fully self-contained
3. **Do NOT add dependencies beyond paho-mqtt and python-dotenv** — keep it minimal
4. **Do NOT encrypt the MQTT payloads** — encryption is a Phase 4 concern. The pump simulates current firmware behavior.

### Build Verification

```bash
# Verify plugin structure
ls plugins/test-pump/plugin.json
ls plugins/test-pump/pump.py
ls plugins/test-pump/scenarios/healthy_fleet.json
ls plugins/test-pump/scenarios/low_battery.json

# Verify JSON is valid
python -c "import json; json.load(open('plugins/test-pump/plugin.json'))"
python -c "import json; json.load(open('plugins/test-pump/scenarios/healthy_fleet.json'))"

# Test run (requires MQTT broker on localhost:1883)
cd plugins/test-pump
pip install -r requirements.txt
python pump.py --scenario healthy_fleet --interval 2
```

### Success Criteria

1. `plugins/test-pump/` directory exists with all files listed above
2. `plugin.json` is valid and follows the `magic-plugin-v1` schema
3. `pump.py` connects to MQTT and publishes telemetry for all devices in the scenario
4. Two scenario files exist: `healthy_fleet.json` and `low_battery.json`
5. Running the pump + subscribing to `magic/#` in an MQTT client shows realistic device data
6. Ctrl+C stops the pump gracefully

## PROMPT END
