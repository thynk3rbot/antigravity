# LoRaLink Architecture Partition
## Evidence-Based Responsibility Boundaries

**Authored:** Claude (with AG input required)
**Date:** 2026-03-26
**Status:** PROPOSED — Requires explicit sign-off from both Claude and AG before any new implementation

---

## Executive Summary

**Previous Approach (RTOS Era):** Device-centric intelligence → **Result:** Unstable, complexity bloat, lost velocity
**Proposed Approach (Daemon-Centric):** Dumb devices + smart daemon → **Predicted Result:** Stable, maintainable, incremental releases

**Core Principle:** *Devices are GPIO actuators. Intelligence lives in the daemon.*

---

## Problem Statement (Historical Evidence)

| Era | Architecture | Outcome |
|-----|--------------|---------|
| **Pre-RTOS** | Working product (V1) | Stable, users happy |
| **RTOS Migration** | Moved logic to device FreeRTOS tasks | Complexity increased, bugs emerged |
| **Current (V0.0.11)** | Devices trying to be smart (scheduling, routing, status aggregation) | Unstable boot, GPIO conflicts, NMEA flooding, no releases |

**Root Cause Analysis:**
- Each device is trying to do too much (schedule tasks, route commands, aggregate status)
- RTOS complexity ≠ capability — it introduced preemption, race conditions, memory fragmentation
- No separation of concerns: device firmware = provisioning + routing + scheduling + GPIO control = unmaintainable

**Solution:** Return to simple devices + move intelligence to daemon (which is already there).

---

## Proposed Three-Layer Partition

### Layer 1: Device Firmware (Minimal)
**Responsibility:** GPIO actuator + basic telemetry
**Complexity:** ~2KB code (after cleanup)
**Stability Requirement:** 99.9% uptime (mesh nodes must stay alive)

#### Device API (Command Set)

**Inbound (from daemon via MQTT/BLE):**
```
// GPIO Control
{"cmd": "gpio_set", "pin": 32, "state": 1}                    // Set HIGH/LOW
{"cmd": "gpio_toggle", "pin": 32, "duration_ms": 1000}        // Pulse
{"cmd": "gpio_read", "pin": 32}                               // Get current state
{"cmd": "pwm_set", "pin": 13, "freq_hz": 1000, "duty": 128}   // PWM control

// Status
{"cmd": "status"}                                              // Report status
{"cmd": "ping"}                                                // Heartbeat

// Configuration
{"cmd": "config_save", "net_secret": "abc123"}                 // Store secret (one-time)
```

**Outbound (to daemon):**
```
// Status Report (every 10 seconds or on change)
{"msg_type": "status",
 "node_id": "node-30",
 "uptime_ms": 123456,
 "battery_mv": 4200,
 "relay_states": [1, 0, 1, 0, 1, 0, 0, 0],  // 8 relays
 "oled_page": 3,
 "heap_free": 184320,
 "peers": ["node-28", "node-42"]
}

// Command Acknowledgment
{"msg_type": "ack",
 "cmd_id": "xyz789",
 "status": "ok",
 "result": {"pin": 32, "state": 1}
}

// Error Report
{"msg_type": "error",
 "cmd_id": "xyz789",
 "error": "GPIO 21 unavailable (VEXT conflict)"
}
```

**What Device Does NOT Do:**
- ❌ Schedule tasks (daemon does this)
- ❌ Route commands between peers (daemon does this)
- ❌ Aggregate complex status (daemon does this)
- ❌ Execute logic trees (daemon does this)
- ❌ Manage provisioning workflows (daemon does this)

**Device Firmware Size Target:**
- Current: ~1.3MB (bloated with OLED, scheduling, routing, status aggregation)
- Target: ~256KB (GPIO + BLE/MQTT + telemetry)
- Benefit: Leaves 3.7MB+ for OTA, libraries, robustness

---

### Layer 2: Daemon (Intelligence & Orchestration)
**Responsibility:** Configuration, routing, scheduling, provisioning
**Complexity:** Unbounded (can grow as needed)
**Stability Requirement:** 95% uptime (can restart, recovers state from NVS)

#### Daemon API (HTTP REST + MQTT)

**HTTP Endpoints (to Webapp):**
```
GET /api/devices                    → List all nodes, their status, last-seen
GET /api/device/{node_id}/status    → Current state (battery, uptime, relays)
POST /api/device/{node_id}/command  → Send GPIO command to device
  {"pin": 32, "action": "toggle", "duration_ms": 1000}

POST /api/schedule/{node_id}        → Schedule future command
  {"at": "2026-03-26T09:00:00", "pin": 32, "action": "set", "state": 1}

GET /api/mesh/graph                 → Peer connectivity (signal strength, routing)
POST /api/provision                 → Onboard new device
  {"mac": "aa:bb:cc:dd:ee:ff", "node_id": "node-50", "net_secret": "..."}
```

**MQTT Topics (to/from Device):**
```
device/{node_id}/cmd       → Command queue (daemon → device)
device/{node_id}/status    → Status telemetry (device → daemon)
device/{node_id}/ack       → Acknowledgments (device → daemon)
```

**Daemon Responsibilities:**
- Schedule management (TaskScheduler, cron, one-off timers)
- Command routing (figure out which transport: LoRa, BLE, WiFi)
- Provisioning workflows (generate secrets, distribute to devices)
- Status aggregation (collect from all devices, expose via REST)
- Mesh visualization (track peer connectivity, signal strength)
- OTA deployment (push firmware updates to devices)
- Persistent state (remember which relays were on, schedules, etc.)

---

### Layer 3: Webapp (User Interface)
**Responsibility:** Dashboard, device discovery, command queuing
**Complexity:** Moderate (UI + REST client)
**Stability Requirement:** Can be stateless (daemon is source of truth)

#### Webapp Features
- Real-time dashboard (device status, peer graph)
- Device control panel (toggle relays, set GPIO)
- Schedule builder (create recurring commands)
- Provisioning wizard (onboard new devices)
- OTA management (upload firmware, deploy to fleet)
- Historical logs (command history, error logs)

**Webapp Does NOT Do:**
- ❌ Store device state (daemon does)
- ❌ Execute commands directly on devices (goes through daemon)
- ❌ Manage credentials (daemon does)

---

## API Boundaries (Ironclad Contracts)

### Device ↔ Daemon Contract
**Protocol:** MQTT or BLE serial
**Latency SLA:** <2 seconds for command delivery
**Message Format:** JSON (fixed schema per command type)
**Idempotency:** All commands include `cmd_id`; device acknowledges with `cmd_id` + status

**Contract Enforcement:**
- Device validates JSON before executing
- Device ignores malformed commands
- Device echoes `cmd_id` in acknowledgment
- Daemon retries with exponential backoff if no ack after 5s

### Daemon ↔ Webapp Contract
**Protocol:** HTTP REST
**Latency SLA:** <500ms for status queries, <2s for command execution
**Message Format:** JSON with typed fields
**Caching:** Webapp caches status for 5 seconds (daemon is authoritative)

**Contract Enforcement:**
- All endpoints return consistent HTTP status codes
- All errors include `error_code` + human-readable `message`
- Daemon publishes OpenAPI spec for Webapp to validate against

---

## Evidence: Why This Works

### Simplicity ✅
- Device firmware: ~256KB vs current ~1.3MB
- Fewer edge cases (device just executes commands, doesn't reason)
- Fewer race conditions (no multi-tasking logic on device)

### Stability ✅
- V2 boot hang (GPIO 21 conflict) = device trying to do too much
- If device only controlled GPIO, no interference possible
- Each layer isolated: device dies → daemon recovers; daemon dies → device keeps running

### Velocity ✅
- Device firmware stable → can focus on daemon features
- Daemon owns scheduling/routing → can iterate without flashing devices
- Webapp independent → can update dashboard without touching hardware

### Maintainability ✅
- Clear responsibility: device = transport layer, daemon = business logic
- New features = daemon changes, not device changes
- Easier to test: mock device API, test daemon logic

---

## Implementation Roadmap (Evidence-Based)

### Phase 0: Simplify Device (Week 1)
**Evidence:** Current V2 instability is device bloat
**Action:** Strip device to GPIO + status + BLE

- Remove: ScheduleManager, CommandManager routing logic, StatusBuilder aggregation
- Keep: GPIO control, battery monitoring, OLED (status display only)
- Target: V2 stable boot, V3/V4 baseline

**Definition of Done:**
- All three variants (V2, V3, V4) boot reliably (10+ min uptime)
- Device accepts GPIO commands via BLE
- STATUS command works
- No watchdog resets, no OLED corruption

### Phase 1: Daemon API (Week 2)
**Evidence:** Device can't stay dumb if daemon has no interface
**Action:** Implement REST endpoints for device discovery, command sending, status polling

- Define Device ↔ Daemon contract (MQTT topics, JSON schema)
- Implement /api/devices, /api/device/{id}/status, /api/device/{id}/command
- Implement status aggregation (daemon polls devices, caches, serves to Webapp)
- Provisioning API (assign node_id, distribute secrets)

**Definition of Done:**
- POST /api/device/node-30/command toggles a relay
- GET /api/devices returns all node statuses
- Provisioning wizard onboards a new device

### Phase 2: Webapp Dashboard (Week 3)
**Evidence:** Daemon features are useless without UI
**Action:** Build real-time dashboard consuming daemon REST API

- Device status panel (battery, uptime, relay states)
- Quick command buttons (toggle relays, read GPIO)
- Peer graph visualization (who can talk to whom)
- Command history log

**Definition of Done:**
- Click relay button → device toggles
- Dashboard updates within 5 seconds
- No hardcoded IPs/device IDs (all from daemon discovery)

### Phase 3: Scheduling & Provisioning (Week 4+)
**Evidence:** Once core is solid, add features incrementally
**Action:** Daemon scheduling, multi-device provisioning

- Daemon TaskScheduler owns all schedules
- Webapp schedule builder creates commands
- Provisioning daemon generates node IDs, secrets, distributes to devices

---

## Partition of Duties (Claude vs AG)

### Claude (Architecture & Daemon)
- Design API contracts (device ↔ daemon, daemon ↔ webapp)
- Implement daemon (provisioning, routing, scheduling, status aggregation)
- Plan testing strategy, define definition of done
- Code review (ensure contracts are honored)

### AG (Device Firmware & Validation)
- Simplify device firmware (strip bloat, keep GPIO + status)
- Flash devices, validate hardware behavior
- Provide feedback on API design (does it work on real hardware?)
- Integration testing (device + daemon together)

### Both (Daily Cadence)
- **09:00 — Plan:** What's the sprint goal? (e.g., "Device GPIO API stable")
- **10:00 — Decide:** Who does what? (Claude: API spec; AG: firmware changes)
- **11:00 — Implement:** Claude codes daemon; AG codes firmware
- **14:00 — Deploy:** Push changes to staging
- **15:00 — Test:** AG flashes devices, validates; Claude validates API responses
- **17:00 — Release:** Tag version, ship if all green

---

## Evidence Summary

| Claim | Evidence | Outcome |
|-------|----------|---------|
| Device bloat caused instability | V2 GPIO 21 conflict, VEXT race condition, NMEA flooding | Simplify device |
| Dumb devices + smart daemon works | Previous V1 (working product) | Restore this architecture |
| Clear APIs prevent scope creep | Device contract limits what firmware can do | Faster iterations |
| Partition enables parallelism | Claude (daemon) ≠ AG (firmware) work independently | 2x velocity |
| Incremental releases possible | Each phase (0-3) ships independent feature | Users see progress |

---

## Sign-Off Required

**Before any new implementation:**

- [ ] Claude agrees to partition and API contracts
- [ ] AG agrees to partition and device firmware scope
- [ ] Both agree to daily cadence (Plan → Decide → Implement → Deploy → Test)

**Once signed off:**
- No feature creep to device firmware (daemon owns features)
- No shortcuts on API contracts (ironclad abstraction)
- No skipping planning phase (evidence-based decisions only)

---

## Questions for Negotiation

1. **Device firmware scope:** Is GPIO + status + BLE enough, or must device have scheduling?
2. **Daemon deployment:** PC daemon only, or also embedded (e.g., on V4 as local hub)?
3. **Mesh routing:** Does daemon need full mesh topology, or just point-to-point?
4. **OTA strategy:** How to push firmware updates without losing devices?
5. **Data persistence:** Where does device state live? (NVS on device, or daemon database?)

---

**Next Step:** Claude + AG negotiate and sign off on this partition.
**Do Not Proceed:** With implementation until both parties agree in writing.
