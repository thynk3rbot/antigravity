# End-to-End Magic Workflow

Complete guide: from virgin devices to production mesh at scale.

---

## 1️⃣ Factory: Commission Virgin Devices

### Prerequisites
- Heltec devices (V3 or V4) — unopened, never flashed
- USB cable and port
- PlatformIO installed: `pip install platformio`

### Single Device

1. **Connect device** via USB
2. **Double-click** `Factory_USB_Flasher.bat`
3. **Select device** (auto-detected if only one connected)
4. **Confirm hardware version** (V3 or V4)
5. **Type `yes`** to start flash (~2 minutes)
6. **Enter device ID** (e.g., `GATEWAY_01`, `SENSOR_03`)
7. **Done** — device is now commissioned

👉 **Full guide:** [FACTORY_COMMISSIONING.md](FACTORY_COMMISSIONING.md)

### Batch Commissioning

For 10+ devices at once:

```bash
# Create CSV file with port,hardware_version,device_id
# e.g.,
# COM3,v4,GATEWAY_01
# COM4,v4,SENSOR_03
# COM5,v3,SENSOR_05

python tools/usb_flasher.py --batch devices.csv

# Flasher will process all devices sequentially with auto-registration
```

---

## 2️⃣ Infrastructure: Start Magic Platform

### One-Click Launch

From repo root:

```powershell
# Windows
Start_Magic.bat

# Linux/Mac
python daemon/src/main.py --port 8001 --mqtt-broker localhost:1883
```

This starts:
- ✓ MQTT Broker (port 1883) — message bus for all devices
- ✓ Magic Daemon (port 8001) — REST API, fleet manager, mesh gateway
- ✓ Fleet Dashboard (port 8000) — web UI for monitoring and control
- ✓ Magic Messenger (port 8400) — mesh chat bridge
- ✓ AI Assistant (port 8300) — local AI with mesh context

**Wait ~10 seconds** for all services to initialize. You should see the octopus (🐙) in the system tray.

### Headless Mode (Production)

For background/supervised startup:

```bash
python tools/start_bg_services.py

# Stop with:
python tools/start_bg_services.py stop

# Logs in: logs/ directory
```

---

## 3️⃣ Commissioning: Devices Boot Online

Once daemon is running, commissioned devices will:

1. **Boot** and load stored WiFi credentials
2. **Connect to WiFi** (or use fallback AP mode)
3. **Send NODE_ANNOUNCE** packet advertising capabilities (WiFi, LoRa, BLE, etc.)
4. **Register in fleet** — daemon receives announce and updates registry
5. **Appear in Dashboard** within 30-60 seconds

### Check Device Status

Open [http://localhost:8000](http://localhost:8000):

```
Fleet Dashboard
├─ GATEWAY_01 (online, 3400mV, -95 dBm, v0.0.154V4)
├─ SENSOR_03 (online, 3100mV, -105 dBm, v0.0.154V4)
└─ SENSOR_05 (online, 4200mV, -88 dBm, v0.0.154V3)
```

Click device → see telemetry, battery, RSSI, uptime, location (if GPS).

---

## 4️⃣ Configuration: Deploy Features

### Factory Admin Panel

Open [http://localhost:8000/factory.html](http://localhost:8000/factory.html):

- **Device Registry** — view/edit device metadata
- **Manual Registration** — add devices without USB flasher
- **Bulk Import** — import from JSON, CSV, or XLSX
- **OTA Flash** — single-device firmware update

### Set Device Configuration (HTTP)

Change device network settings:

```bash
# Set static IP + gateway + subnet
curl -X POST http://device_ip/api/cmd \
  -d "SETIP 192.168.1.42 192.168.1.1 255.255.255.0"

# Set MQTT broker address
curl -X POST http://device_ip/api/cmd \
  -d "SETBROKER mqtt.local 1883"

# Reboot
curl -X POST http://device_ip/api/cmd \
  -d "REBOOT"
```

Or **via MQTT** from daemon:

```bash
# Publish command to device
mosquitto_pub -h localhost -t "magic/GATEWAY_01/cmd" -m "SETIP 192.168.1.42 192.168.1.1 255.255.255.0"
```

---

## 5️⃣ Operations: Monitor Fleet Health

### Dashboard Views

**Fleet Status** (main page):
- Device list with battery/RSSI/version
- Topology visualization (peer connections)
- Command history
- Service health

**Swarm OTA Panel** (right sidebar):
- Select devices to update
- Choose V3 or V4 firmware
- Flash multiple devices in parallel
- Real-time progress bars
- Auto-verify: version increment confirms success

**Device Detail** (click device):
- Full telemetry (battery, uptime, neighbors)
- Recent commands + responses
- Logs + debug info
- Network metrics

### API Endpoints

Check fleet health programmatically:

```bash
# Daemon health
curl http://localhost:8001/health
# → {"status":"healthy","peers":47,"services_running":5}

# Mesh topology
curl http://localhost:8001/api/mesh/topology
# → {"node_count":47,"online_count":45,"peers":[...]}

# Device status
curl http://localhost:8001/api/mesh/node/GATEWAY_01
# → {"node_id":"GATEWAY_01","battery_mv":3400,"uptime_ms":...}

# Send command (intelligent routing)
curl -X POST http://localhost:8001/api/mesh/command \
  -H "Content-Type: application/json" \
  -d '{
    "target_node": "SENSOR_03",
    "action": "gpio_set",
    "pin": 14,
    "duration_ms": 5000
  }'
```

### Scaling to 1000+ Devices

Magic uses **three complementary patterns** for global scale:

#### Pattern 1: HTTP Gateway (Any Network)
- Daemon routes commands directly to device IPs from registry
- Fallback to MQTT if device offline
- No firmware changes needed

#### Pattern 2: Peer Ring (Deterministic Routing)
- Consistent hashing: `hash(device_id) → responsible_peer`
- Same answer everywhere — no lookups needed
- O(log n) hops instead of O(n)

#### Pattern 3: Gossip Protocol (Peer-to-Peer)
- Devices broadcast status every 5 minutes to 3 random neighbors
- Exponential spread → all peers know updates in log(n) rounds
- No central broker needed for state dissemination

👉 **Full architecture:** [SCALE_TO_1000S.md](SCALE_TO_1000S.md)

---

## 6️⃣ Testing: Validate Commands

### Integration Test Suite

Verify all 24 firmware commands work:

```bash
# Prerequisites
pip install -r tools/requirements.txt

# Test one device
python tools/testing/integration_test.py --ip 192.168.1.42 --transports http,mqtt

# Critical commands only (fast validation)
python tools/nightly_test.py --critical-only --ip 192.168.1.42

# Endurance test (100 cycles, random commands)
python tools/testing/overdrive.py --cycles 100 --delay 10
```

**Output:**
- Per-command latency
- Success/failure rates
- Wiring status (missing implementations)
- JSON report for automation

👉 **Full testing guide:** [tools/testing/TESTING.md](../tools/testing/TESTING.md)

---

## 7️⃣ Deployment: Production Readiness

### Pre-Production Checklist

- [ ] **Fleet Registry Populated** — All devices in `daemon/data/device_registry.db`
- [ ] **WiFi Credentials Deployed** — Devices know SSID/password
- [ ] **MQTT Broker Running** — Mosquitto on 1883 or configured alternative
- [ ] **Daemon Stable** — Running 24+ hours without crashes
- [ ] **Dashboard Responsive** — All UI panels load in <2s
- [ ] **OTA Flashing Tested** — At least one device updated via swarm OTA
- [ ] **Integration Tests Pass** — All critical commands respond
- [ ] **Logging Configured** — Logs written to `logs/` for audit trail

### Startup on Reboot (Windows)

Register as Windows Startup Task:

```powershell
# Run as Administrator
Right-click Register_Startup_Task.bat → "Run as Administrator"

# Magic will now start on every login
```

For remote/headless servers, use background mode:

```powershell
# Auto-restart on crash
python tools/start_bg_services.py

# Logs at: logs/daemon.log, logs/webapp.log, etc.
```

### Monitoring at Scale

Track fleet health with scripts:

```bash
# Count online devices
curl -s http://localhost:8001/health | jq .peers_online

# List offline devices
curl -s http://localhost:8001/api/mesh/topology | jq '.peers[] | select(.reachable == false)'

# Check battery levels across fleet
curl -s http://localhost:8001/api/mesh/topology | \
  jq '.peers[] | {node: .node_id, battery_mv: .battery_mv}'
```

---

## 🔄 Common Operations

### Update Firmware (All Devices)

1. Open [http://localhost:8000](http://localhost:8000)
2. Click **Swarm OTA** (right sidebar)
3. Click **Select All** (or manually select devices)
4. Choose hardware version (V3/V4)
5. Click **Flash** — watch progress bars
6. Confirm: each device version increments

### Add New Device (Mid-Deployment)

1. Flash via `Factory_USB_Flasher.bat` or `usb_flasher.py`
2. Device boots and announces itself
3. **Wait 1 minute** — appears in Fleet Dashboard automatically
4. Device is ready to use

### Debug a Failing Device

```bash
# Get full device status
curl http://localhost:8001/api/mesh/node/SENSOR_03

# Check connectivity via ping
curl -X POST http://localhost:8001/api/mesh/command \
  -H "Content-Type: application/json" \
  -d '{"target_node":"SENSOR_03","action":"status"}'

# If not responding, try direct USB connection
python tools/ble_instrument.py --device SENSOR_03 --verbose

# Last resort: re-flash via USB
Factory_USB_Flasher.bat --port COM3 --hw v4
```

### Export Fleet Data

Backup device registry:

```bash
# Export as JSON
sqlite3 daemon/data/device_registry.db ".mode json" \
  "SELECT * FROM device_registry;" > fleet_backup.json

# Or use daemon API
curl http://localhost:8001/api/registry/devices > fleet_snapshot.json
```

---

## 📚 Documentation Map

| Document | Purpose |
| --- | --- |
| [STARTUP.md](../STARTUP.md) | Quick-start (1-2 minutes) |
| [FACTORY_COMMISSIONING.md](FACTORY_COMMISSIONING.md) | Virgin device provisioning (AG only) |
| [SCALE_TO_1000S.md](SCALE_TO_1000S.md) | Architectural patterns for global scale |
| [operations.html](operations.html) | Daemon architecture + MQTT contract |
| [tools/testing/TESTING.md](../tools/testing/TESTING.md) | Integration testing framework |
| [END_TO_END_WORKFLOW.md](END_TO_END_WORKFLOW.md) | This document — complete lifecycle |
| [CLAUDE.md](../CLAUDE.md) | Dev team — architecture decisions + coupling rules |

---

## ⚡ Quick Reference

```bash
# Start everything
Start_Magic.bat

# Flash one virgin device
Factory_USB_Flasher.bat

# Test critical commands
python tools/nightly_test.py --critical-only --ip 192.168.1.42

# Monitor daemon logs (live)
tail -f logs/daemon.log

# Send command to device (via HTTP)
curl -X POST http://device_ip/api/cmd -d "STATUS"

# Check fleet health
curl http://localhost:8001/health | jq

# Export device list
sqlite3 daemon/data/device_registry.db "SELECT device_id, status FROM devices;" | column -t -s '|'
```

---

**Status:** Production Ready ✓ | **Last Updated:** 2026-03-31 | **Version:** 1.0
