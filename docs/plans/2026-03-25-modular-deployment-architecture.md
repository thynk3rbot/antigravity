# Modular Deployment Architecture

**Date:** 2026-03-25
**Status:** Design approved, implementation pending
**Authors:** Claude + User, with AG (Gemini) firmware feasibility input

## Problem

The current build/deploy model requires building separate firmware for each board variant and device role. Configuration is scattered across compile-time flags, hardcoded constants, and manual NVS writes. There's no provisioning pipeline — each device is set up individually via serial commands.

This doesn't scale from 4 test devices to 10,000 production devices.

## Design: Three-Tier Deployment

```
TIER 1 — FACTORY:  Build ONE firmware per board variant (V2/V3/V4).
                    All features compiled in. Board variant is the only
                    compile-time decision (different radios).

TIER 2 — FLEET:    Daemon provisions devices with identity + config.
                    NVS Feature Registry enables/disables features.
                    Carrier profile defines hardware topology.
                    Product JSON defines device behavior. No reflash.

TIER 3 — USER:     Webapp/device UI for runtime tuning.
                    Schedules, pin assignments, alerts. Non-destructive.
```

## Phase 1 Scope (implement now)

### Firmware: NVS Feature Registry

Three NVS namespaces, three concerns:

**`features` namespace** — what software to activate (u8 per feature, 0=off 1=on):
- `relay`, `mqtt`, `gps`, `ble`, `espnow`, `sensor`, `oled`, `scheduler`, `mcp`
- Default: ALL ON. Disabling is opt-out. Fresh device works immediately.

**`hw` namespace** — what hardware is connected:
- `i2c_buses` (u8), `i2c0_sda/scl` (u8), `i2c1_sda/scl` (u8)
- `mcp_count` (u8), `mcp_addrs` (blob), `mcp_bus` (u8), `mcp_int_pin` (u8)
- `carrier` (str) — carrier board profile name

**`mesh` namespace** — device identity and topology:
- `role` (str): "node" / "hub" / "relay" / "gw"
- `topology` (str): "mesh" (default) / "star" / "hybrid"
- `net_secret` (blob): 16-byte network encryption secret
- `fleet_id` (str): fleet grouping identifier

### Firmware: Boot Sequence Integration

```
BootSequence::execute()
  → NVSManager::init()
  → PluginManager::loadFeatureRegistry()  // reads "features" namespace
  → initHAL()       // reads "hw" namespace for I2C/MCP config
  → initTransports() // skips disabled transports
  → initApplication() // skips disabled managers
```

Managers that check the registry before init:
- MQTTManager: skip if `mqtt=0`
- GPSManager: skip if `gps=0`
- BLEManager: skip if `ble=0`
- ESPNowManager: skip if `espnow=0`
- MCPManager: skip if `mcp=0`
- SensorHAL: skip if `sensor=0`
- OLEDManager: skip if `oled=0`
- ScheduleManager: skip if `scheduler=0`

Always-on (required for basic operation):
- NVSManager, PowerManager, WiFiManager, LoRaManager, CommandManager, MessageRouter
- **ScheduleManager** — core control mechanism, not optional. V1's TaskScheduler precision is battle-tested and required for industrial timing (irrigation valves, relay sequencing). Treat as infrastructure, not a feature toggle.

### Firmware: Provisioning Endpoint

New HTTP endpoint on the device webserver:

```
POST /api/provision
Content-Type: application/json

{
  "features": { "mqtt": 0, "gps": 0, "sensor": 1, "relay": 1 },
  "hw": { "carrier": "rv12v", "mcp_count": 1, "mcp_addrs": [32] },
  "mesh": { "role": "node", "topology": "mesh", "fleet_id": "farm-north" },
  "identity": { "name": "Valve-North" },
  "reboot": true
}

Response: { "status": "ok", "reboot_in_ms": 2000 }
```

Also: `GET /api/config` — returns current NVS config as JSON.

### Carrier Board Profiles

LittleFS `/carriers/` directory. JSON files define hardware topology for known carrier boards:

```json
{
  "id": "rv12v",
  "name": "RV 12V Control Board",
  "hw": { "i2c_buses": 1, "mcp_count": 1, "mcp_addrs": [32] },
  "features": { "relay": 1, "mcp": 1, "oled": 1, "scheduler": 1 },
  "pins": { "relay_12v_1": 46, "relay_12v_2": 6, "relay_12v_3": 7, "relay_110v": 5 }
}
```

When a carrier is selected, firmware reads the profile and writes `hw` + `features` namespaces.

### Daemon: Single-Device Provisioning

New daemon endpoint:

```
POST /api/provision
{
  "device_id": "magic-27",
  "carrier": "rv12v",
  "product": "greenhouse-valve",
  "identity": { "name": "Valve-North", "role": "node", "fleet_id": "farm-north" }
}
```

Daemon resolves carrier profile + product JSON from local library, merges configs, pushes to device via best available transport.

### Webapp: Provisioning UI

Add to device detail panel:
1. Carrier board dropdown (populated from `/carriers/` on device or daemon library)
2. Product dropdown (populated from `/products/`)
3. Identity fields (name, role, fleet)
4. "Provision" button → daemon POST → device reboot

### Daemon: OTA Firmware Distribution (promoted to Phase 1)

USB is unreliable (port enumeration, driver issues, physical access). OTA is the primary deployment channel.

**Daemon firmware library:**
```
tools/daemon/firmware/
  heltec_v2_0.5.00.bin
  heltec_v3_0.5.00.bin
  heltec_v4_0.5.00.bin
```

**New daemon endpoints:**
```
POST /api/ota
{
  "device_id": "magic-27",
  "firmware": "heltec_v3_0.5.00.bin"
}

GET /api/firmware          → list available binaries
POST /api/firmware/upload  → upload new binary to library
```

**OTA flow:**
1. Claude/PIO builds firmware → binary lands in `daemon/firmware/`
2. Daemon `POST /api/ota` → streams binary to device via HTTP (ESP32 OTA partition)
3. Device validates, writes to OTA partition, reboots
4. If boot fails → automatic rollback to previous partition (ESP-IDF native feature)

**Webapp UI:**
- "Firmware" tab in device panel
- Shows current version, available updates
- "Flash" button → daemon OTA push
- Progress bar via WebSocket

**Device-side requirement:**
- `WiFiManager` already has OTA handler (`ArduinoOTA` or `httpUpdate`)
- Need: `GET /api/version` endpoint returning `{ "version": "0.5.00", "board": "V3" }`
- Daemon uses version + board to select correct binary

## Future Phases

### Phase 2: Batch Provisioning
- Fleet manifest files (CSV/JSON) listing device_mac → carrier → product
- `POST /api/provision/batch` daemon endpoint
- CLI tool for headless provisioning

### Phase 3: Config Library
- Daemon hosts shareable library of carriers + products
- Version-controlled config repo (GitOps)
- Daemon watches for changes, auto-pushes to matching devices

### Phase 4: OTA Module System
- LittleFS-hosted Lua/WASM scripts for custom logic
- Push modules without reflashing core firmware
- Module marketplace concept

### Phase 5: Staged Rollouts
- Canary deployment (push to 1 device, verify, then fleet)
- Automatic rollback on boot failure
- Deployment health scoring

## Key Files to Modify

### Firmware (AG's domain)
- `firmware/magic/lib/App/plugin_manager.cpp/h` — extend as Feature Registry
- `firmware/magic/lib/App/nvs_manager.cpp/h` — add `features`, `hw`, `mesh` namespace accessors
- `firmware/magic/lib/App/boot_sequence.cpp` — conditional init based on registry
- `firmware/magic/lib/App/http_api.cpp` — add `/api/provision` and `/api/config` endpoints
- `firmware/magic/src/main.cpp` — wire PluginManager registry into boot

### Daemon (Claude's domain)
- `tools/daemon/api.py` — add `/api/provision` endpoint
- `tools/daemon/models.py` — add CarrierProfile, ProvisionRequest models
- `tools/daemon/transport.py` — provision command routing

### Webapp (Claude's domain)
- `tools/webapp/server.py` — proxy provision endpoint
- `tools/webapp/static/index.html` — provisioning UI panel

## Verification

1. Flash generic V3 firmware to a device
2. `GET /api/config` returns default (all features ON)
3. `POST /api/provision` with carrier="bare", features={mqtt:0, gps:0}
4. Device reboots, `GET /api/config` shows updated config
5. MQTT and GPS managers do NOT initialize (verify via serial log)
6. All other features work normally

---

## Implementation Status (2026-03-25)

### ✅ COMPLETED

**Firmware (v2):**
- NVS Feature Registry: Three namespaces (`features`, `hw`, `mesh`) with u8 per feature
- Boot Sequence: Conditional init based on `PluginManager::isEnabled(feature_name)`
- Provisioning Endpoint: `POST /api/provision` and `GET /api/config` on device
- I2C Mutex: FreeRTOS binary semaphore for shared-bus protection
- All Features Toggleable: relay, mqtt, gps, ble, espnow, sensor, oled, scheduler, mcp
- Always-On: NVSManager, PowerManager, LoRaManager, MessageRouter, ScheduleManager, CommandManager

**Daemon (tools/daemon/):**
- Models: ProvisionRequest, ProvisionResponse, CarrierProfile data classes
- Provision Endpoint: `POST /api/provision` with carrier profile merging
- Carrier Profiles: bare.json, rv12v.json (template examples)
- Endpoint: `GET /api/carriers` to list available profiles

**Testing:**
- E2E Test Harness: tools/tests/test_provisioning_e2e.py
- Unit Tests: Feature merging, carrier loading, response models
- Integration Tests: Feature Registry logic, hardware topology provisioning

### 📋 READY FOR NEXT PHASE

**Phase 2: Batch Provisioning**
- Fleet manifest (CSV/JSON) → daemon → devices
- `POST /api/provision/batch` endpoint
- Headless CLI for production deployment

**Phase 3: Config Library**
- Daemon-hosted carrier + product library
- GitOps workflow: watch repo, auto-push to devices
- Version-controlled configurations

### 📝 NOTES FOR IMPLEMENTATION

1. **Carrier Profiles Live in:** `tools/daemon/carriers/*.json`
   - Add new profiles for custom hardware without firmware recompile
   - Each profile defines `hw`, `features`, `pins` topology

2. **Feature Toggle Resolution:**
   - Firmware defaults to ALL ON (permissive)
   - Provisioning writes NVS, device reboots
   - Boot reads NVS, skips disabled managers
   - No reflash needed

3. **Hardware Topology:**
   - `hw` namespace configures I2C buses, MCP addresses, pin mappings
   - Carrier profiles encode board-specific topology
   - Device HAL respects configured topology at runtime

4. **Three-Device Constraint Handling:**
   - V2 (16MB Flash, low RAM): disable mqtt, gps, sensor → bare profile
   - V3 (full-featured): enable all → rv12v profile
   - V4 (GPS support): enable gps → custom profile with GPS pins
   - All use same compiled binary, different configurations

5. **Provisioning Flow:**
   ```
   Daemon (POST /api/provision)
     → Loads carrier profile (e.g., rv12v.json)
     → Merges with product config (if provided)
     → Merges with explicit feature overrides
     → Sends merged config to device
     → Device writes to NVS, reboots
     → Device reads NVS, boots with new config
   ```

6. **Future: OTA Module System**
   - Phase 4: Push Lua/WASM scripts via LittleFS
   - Extend device behavior without reflash
   - Complements Feature Registry for custom logic
