# Build & Flash Instructions for AG

## Latest Changes (Merged to `main`)

✅ **PR #6 Merged** - Hybrid proxy webapp integration + V2 responsiveness fixes + Phase 50 architecture

### What's New
- 🔗 Hybrid Proxy tab in webapp (live status, metrics, cost tracking)
- ⚡ V2 firmware responsiveness fixes (50-200ms → <20ms latency)
- 📡 Pure mesh architecture (Phase 50)
- 🛠️ Operations tooling (batch files, daemon improvements)

---

## Quick Start

### 1. Pull Latest Code
```bash
cd C:\Users\spw1\Documents\Code\Antigravity
git fetch origin
git pull origin main
```

### 2. Build Firmware

**For V2 (Active Development)**
```bash
cd firmware/v2
pio run -e heltec_wifi_lora_32_V3
```

**For All Platforms**
```bash
# V2
pio run -e heltec_wifi_lora_32_V3

# V3
pio run -e heltec_wifi_lora_32_V3 --project-option="PLATFORMIO_BOARD=heltec_v3"

# V4
pio run -e heltec_wifi_lora_32_V3 --project-option="PLATFORMIO_BOARD=heltec_v4"
```

### 3. Flash to Device

**Via USB Serial (Direct)**
```bash
pio run -t upload
```

**Via OTA (Over-The-Air)**
```bash
# Master device
pio run -e ota_master

# Slave device  
pio run -e ota_slave
```

### 4. Monitor Output
```bash
pio device monitor -b 115200
```

---

## Version Management

Before flashing, bump firmware version:

```bash
# Show current version
tools/version.sh current 2

# Bump point release (0.0.XX → 0.0.YY)
tools/version.sh bump point 2

# Or set specific version
tools/version.sh set 0.1.00-2

# Validate all
tools/version.sh validate-all
```

**Required:** Version MUST be bumped for every flash. Build system will warn if not updated.

---

## Fleet Test Workflow

If running fleet test tonight:

```bash
# 1. Build V2 firmware
cd firmware/v2
pio run -e heltec_wifi_lora_32_V3

# 2. Flash all devices
pio run -t upload  # Via USB for each device
# OR
pio run -e ota_master / ota_slave  # Via OTA

# 3. Monitor daemon
python daemon/src/main.py --monitor

# 4. Check Ollama code generation
tools/phase50_operations.bat queue

# 5. View metrics in webapp
python tools/webapp/server.py --device loralink-master
# Open http://localhost:8000 → 🔗 Hybrid Proxy tab
```

---

## Key Files Changed

| File | Change |
|------|--------|
| `firmware/v2/src/main.cpp` | Task priority decoupling |
| `firmware/v2/lib/App/oled_manager.cpp` | Deferred I2C refresh |
| `firmware/v2/lib/App/control_loop.cpp` | Sensor caching |
| `tools/webapp/server.py` | Proxy API endpoints |
| `tools/webapp/static/index.html` | Proxy UI page |
| `daemon/src/mqtt_client.py` | Topic alignment |

---

## Testing After Flash

```bash
# Quick status check
curl http://loralink-master.local/api/status

# Check mesh topology
mqtt_sub loralink/topology

# Verify proxy integration
http://localhost:8000/api/proxy/status

# Monitor V2 responsiveness
# Button press should respond in <20ms (was 50-200ms before)
```

---

## Troubleshooting

**Build fails?**
```bash
pio run --verbose  # See full output
```

**Upload fails?**
```bash
pio run -t erase   # Erase flash
pio run -t upload  # Try again
```

**Monitor shows garbage?**
```bash
pio device monitor -b 115200 --rts 0 --dtr 0
```

---

## Rollback (if needed)

```bash
git log --oneline main
git revert <commit-hash>
git push origin main
```

---

**Questions?** Check CLAUDE.md in root for workflow rules, or review Phase 50 design docs in `docs/`.

Generated: 2026-03-26
