# 🚀 Infrastructure Setup for AG

**Status:** Daemon is ready. Missing prerequisites. Fix below.

---

## Prerequisites to Install (One-Time Setup)

### 1. Mosquitto (MQTT Broker) — REQUIRED

**Windows (Easiest):**
```powershell
# Install via winget
winget install mosquitto

# Or download manually:
# https://mosquitto.org/download/
```

**Verify:**
```bash
mosquitto --version
# Should output version number, e.g., "mosquitto version 2.0.18"
```

### 2. PlatformIO (Firmware Build/Flash) — REQUIRED for firmware work

**Install:**
```bash
pip install platformio
```

**Verify:**
```bash
pio --version
# Should output version number, e.g., "PlatformIO Core 6.1.11"
```

### 3. Python Dependencies (Already Installed) ✓

```bash
# If not installed:
pip install -r daemon/requirements.txt
```

**Verify:**
```bash
python -c "import fastapi, uvicorn, paho.mqtt; print('OK')"
# Should print: OK
```

---

## Quick Check: Is Everything Installed?

```bash
# Run this script to verify all prerequisites:

echo "Checking prerequisites..."
python --version || echo "MISSING: Python"
mosquitto --version || echo "MISSING: Mosquitto"
pio --version || echo "MISSING: PlatformIO"
python -c "import fastapi; print('OK')" || echo "MISSING: Python deps"

echo "All prerequisites installed!" || echo "Install missing items above"
```

---

## Start Infrastructure (After Prerequisites Installed)

### Option A: Windows Batch (Easiest)

```powershell
# From repo root, double-click:
Start_Magic.bat

# Or run from terminal:
Start_Magic.bat

# Expected output:
# - MQTT Broker starting on port 1883
# - Magic Daemon starting on port 8001
# - Magic Dashboard opening at http://localhost:8000
# - System tray shows octopus icon (🐙)
```

### Option B: Manual (For Debugging)

**Terminal 1 — Start MQTT Broker:**
```bash
mosquitto -v
# Should output:
# 1234567890: mosquitto version 2.0.18 starting
# 1234567890: Using default config from /etc/mosquitto/mosquitto.conf
# 1234567890: Opening ipv6 listen socket on port 1883
```

**Terminal 2 — Start Daemon:**
```bash
cd daemon
python src/main.py --port 8001 --mqtt-broker localhost:1883

# Should output:
# [INFO] [Magic] Initializing Magic (Pure Mesh Gateway)...
# [INFO] [Magic] Pulse: Checking Infrastructure...
# [INFO] [Magic] Pulse: Connecting to Magic Bus...
# [INFO] [Magic] ========================================
# [INFO] [Magic]    ALL SYSTEMS NOMINAL (🐙 ACTIVE)
# [INFO] [Magic] ========================================
```

**Browser:**
```
Open: http://localhost:8000
Should see: Fleet Dashboard (blank initially, no devices yet)
```

---

## Verification: Is Infrastructure Up?

```bash
# Check daemon is responding
curl http://localhost:8001/health
# Should return JSON like:
# {"status":"healthy","daemon":"octopus","peers":0,"services_running":5}

# Check MQTT is working
mosquitto_sub -h localhost -t "test" &
mosquitto_pub -h localhost -t "test" -m "hello"
# Should output: hello
```

---

## Common Issues & Fixes

### "mosquitto not found"
```bash
# Install it:
winget install mosquitto

# Or download from: https://mosquitto.org/download/
# Add to PATH if installed to custom location
```

### "pio not found"
```bash
# Install it:
pip install platformio

# Verify:
which pio  # Should show path to pio
```

### "Port 1883 already in use"
```bash
# Another MQTT broker is running
# Option 1: Stop the other one
lsof -i :1883  # See what's using port 1883

# Option 2: Use different port
mosquitto -p 1884

# Option 3: Start fresh Windows session
```

### "Permission denied" starting mosquitto
```bash
# On Windows, run as Administrator
# Right-click Command Prompt → "Run as Administrator"
# Then: mosquitto -v
```

### Daemon crashes on startup
```bash
# Check logs:
tail -f logs/daemon.log

# Common causes:
# - Port 8001 already in use
# - MQTT broker not running
# - Missing Python dependencies

# Fix:
python -c "import fastapi, uvicorn, paho.mqtt"  # Install missing deps
mosquitto -v  # Ensure MQTT is running
# Try daemon again
```

---

## Startup Checklist

- [ ] **Mosquitto installed:** `mosquitto --version` works
- [ ] **PlatformIO installed:** `pio --version` works
- [ ] **Python deps:** `pip install -r daemon/requirements.txt` done
- [ ] **MQTT running:** `mosquitto -v` outputs version info
- [ ] **Daemon running:** `python daemon/src/main.py` starts without errors
- [ ] **Dashboard accessible:** http://localhost:8000 loads
- [ ] **Health check passes:** `curl http://localhost:8001/health` returns JSON

---

## Full Startup Sequence (First Time)

```bash
# Terminal 1: Start MQTT
mosquitto -v
# Wait for: "mosquitto version X.X.X starting"

# Terminal 2: Start Daemon
cd daemon
python src/main.py --port 8001 --mqtt-broker localhost:1883
# Wait for: "ALL SYSTEMS NOMINAL"

# Browser: Open Dashboard
http://localhost:8000
# Should see: Fleet Dashboard (empty initially)

# Verify everything works
curl http://localhost:8001/health
# Should return: {"status":"healthy",...}
```

---

## For Next Time: Quick Start

Once everything is installed:

```bash
# Just run this batch file:
Start_Magic.bat

# Or two terminals:
# Terminal 1: mosquitto -v
# Terminal 2: cd daemon && python src/main.py

# Wait 10 seconds, dashboard should be ready
# http://localhost:8000
```

---

## Next Steps (After Infrastructure is Up)

1. **Build firmware:** `cd firmware/magic && pio run -e heltec_v4`
2. **Flash device:** `pio run -t upload -e heltec_v4`
3. **Check dashboard:** Device should appear at http://localhost:8000

---

## Need Help?

**If infrastructure won't start:**
1. Check all prerequisites are installed (run checklist above)
2. Check ports are available: `netstat -an | grep LISTEN`
3. Check logs: `tail -f logs/daemon.log`
4. Report exact error message to Claude

**If device won't appear:**
1. Check device is powered on
2. Check WiFi is connected (or check serial output)
3. Wait 1 minute (device needs to boot + announce)
4. Check MQTT is receiving telemetry: `mosquitto_sub -h localhost -t "magic/#" -v`

---

**Status:** ✅ Ready to bring up infrastructure

**Next action:** Install Mosquitto + PlatformIO, then run `Start_Magic.bat`

**Expected result:** Dashboard loads at http://localhost:8000
