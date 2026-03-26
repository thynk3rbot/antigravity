# V2 Stability Test Plan — Node 30 (COM7)
**Date:** 2026-03-26
**Firmware:** main branch (ed8d4cf)
**Target Device:** Heltec V2 (Node 30 / COM7)

---

## Phase 1: Boot & Hardware Verification

### Step 1.1 — Power On & Serial Monitor
```bash
# Open serial monitor at 115200 baud
pio device monitor -b 115200
```

**Expected Output (first 5 seconds):**
- No watchdog resets
- No brownout warnings
- Serial output flowing cleanly
- GPIO 21 VEXT pulse logged (if debug enabled)

**PASS Criteria:**
- Device boots to completion without hanging
- No exception codes or stack traces
- OLED displays splash screen or boot progress

**FAIL Criteria:**
- Boot loop (restarts within 5 seconds)
- Stuck on "Initializing..." message
- No serial output at all

---

### Step 1.2 — OLED Display Check
**Visual Inspection (on device):**
1. Does OLED light up immediately on boot? (Should be ~500ms)
2. Can you read the splash screen or status page?
3. Does display respond to button presses within 200ms?
4. Can you rotate pages using the button?

**Expected Pages (in rotation):**
- Page 0: Splash/Title
- Page 1: Network status (WiFi, LoRa, BLE)
- Page 2: GPS fix status (should show "No GPS" on V2)
- Page 3: Relay status (6 relays, CH6 should show "N/A")
- Page 4: System info (IP, uptime, heap)
- Page 5: Peer list (mesh neighbors)

**PASS Criteria:**
- All pages render without corruption
- Button responds within 200ms
- Display doesn't flicker or glitch
- Text is readable

**FAIL Criteria:**
- Partial display (some pixels black/white)
- Button lag >500ms
- Display crashes and goes blank
- Text corrupted or unreadable

---

## Phase 2: Power & Stability

### Step 2.1 — Uptime Test (10+ minutes)
**Action:** Let device run for 10 minutes without commands.

**Monitor for:**
- Watchdog resets (will show `[WATCHDOG]` in logs)
- Heap corruption (erratic free memory)
- Task overflow (stack trace with task name)
- Task hangs (no new log lines for >5 seconds)

**Serial Log Pattern (every ~1 second):**
```
[Power] Battery: 4.2V | Uptime: 0s
[Mesh] Peers: 0 | Signal: N/A
[Status] OLED updated
```

**PASS Criteria:**
- No resets in 10 minutes
- Free heap stays stable (±10KB)
- Status updates every 1 second without gaps

**FAIL Criteria:**
- Watchdog reset
- Heap drops by >100KB
- No logs for >5 seconds (hang)

---

### Step 2.2 — GPIO 21 Conflict Verification
**Expectation:** Relay 6 (CH6) is disabled. Verify it doesn't interfere with VEXT.

**Manual Test:**
```
# Send command to toggle Relay 6 (should be ignored or report "unavailable")
[COMMAND] gpio_toggle 21 1

# Expected response:
[GPIO] Channel 6 (GPIO 21) — DISABLED (conflict with VEXT_PIN)
```

**PASS Criteria:**
- Relay 6 toggle command returns "disabled" or "unavailable"
- OLED Page 3 (Relays) shows CH6 as "N/A" or "---"
- VEXT control continues to work (OLED stays powered)

**FAIL Criteria:**
- Relay 6 toggles and causes OLED to flicker
- VEXT control stops working
- GPIO 21 shows as active in any status

---

## Phase 3: Mesh & Command Routing

### Step 3.1 — BLE Connection Test
**Prerequisites:** Provisioning daemon or BLE terminal app running on PC.

**Action:** Connect via BLE and send a simple STATUS command.
```
# BLE command (raw serial):
STATUS

# Expected response (within 1 second):
{
  "node_id": "node-30",
  "uptime_ms": 12345,
  "battery_v": 4.2,
  "oled_pages": 6,
  "relays_active": 5,
  "peers_count": 0
}
```

**PASS Criteria:**
- BLE connects within 3 seconds
- STATUS command returns JSON in <500ms
- All fields present and valid

**FAIL Criteria:**
- BLE doesn't advertise
- Command hangs (no response after 5 seconds)
- JSON malformed or fields missing

---

### Step 3.2 — LoRa Peer Discovery (if 2+ devices available)
**Prerequisites:** At least 2 V2 devices powered on, same `net_secret` configured.

**Action:**
1. Power on both devices in same room
2. Wait 10 seconds
3. Check OLED Page 5 (Peer List) on both

**Expected:** Devices should see each other as neighbors.

**PASS Criteria:**
- Peer list shows other device's MAC
- Signal strength (RSSI) is displayed (~-70 to -90 dBm)
- Peer doesn't disappear after 30 seconds

**FAIL Criteria:**
- Peer list stays empty
- Peer appears then disappears (unstable link)
- RSSI shows 0 or invalid value

---

### Step 3.3 — Command Routing (if 2+ devices available)
**Prerequisites:** 2+ devices in BLE/LoRa range.

**Action:** Send a GPIO command from Device A to Device B.
```
# On Device A (via BLE):
COMMAND gpio_toggle 32 1000

# Device B should respond (within 2 seconds):
[GPIO] Ch0 toggled (1000ms pulse)
[OLED] Page 3 updated — Relay 0: ON
```

**PASS Criteria:**
- Command arrives at target within 2 seconds
- Relay actuates (if relay exists on that channel)
- Mesh routing logs show `[MESH] Routed to <peer_mac>`

**FAIL Criteria:**
- Command timeout (no response after 5 seconds)
- Wrong device receives command (routing error)
- Relay doesn't actuate despite command confirmation

---

## Phase 4: GPS Isolation (V2 Specific)

### Step 4.1 — GPS Status Check
**Action:** Check OLED Page 2 and logs for GPS references.

**Expected on V2:**
- Page 2 shows "No GPS" (V2 has no GPS module)
- No NMEA strings in LoRa traffic logs
- Status JSON includes `"gps": {"status": "unavailable"}`

**PASS Criteria:**
- GPS gracefully marked as unavailable
- No NMEA spam in mesh logs
- No errors related to GPS

**FAIL Criteria:**
- GPS parser trying to read from wrong port
- NMEA strings appearing in mesh broadcasts
- GPS errors in serial logs

---

## Phase 5: STATUS/VSTATUS Commands

### Step 5.1 — STATUS (Friendly Output)
```
# BLE Command:
STATUS

# Expected: Human-readable JSON
{
  "device": "Node-30",
  "uptime": "12 mins",
  "battery": "4.2V (Good)",
  "network": "LoRa connected",
  "peers": 2,
  "oled": "Page 3/6 (Relays)"
}
```

**PASS Criteria:**
- All fields present
- Values are human-readable (no raw milliseconds)
- Completes in <500ms

---

### Step 5.2 — VSTATUS (Verbose Output)
```
# BLE Command:
VSTATUS

# Expected: Detailed technical JSON
{
  "node_id": "node-30",
  "uptime_ms": 728401,
  "heap_free": 184320,
  "heap_used": 143360,
  "battery_mv": 4189,
  "rssi_dbm": -87,
  "snr_db": 9.5,
  "lora_pkt_recv": 42,
  "lora_pkt_sent": 18,
  "gps": {"status": "unavailable"},
  "relays": [
    {"ch": 0, "gpio": 32, "state": 0},
    {"ch": 1, "gpio": 33, "state": 0},
    ...
    {"ch": 6, "gpio": 21, "status": "disabled"}
  ]
}
```

**PASS Criteria:**
- All technical fields present
- Numbers show actual measurements (not placeholders)
- Relay 6 marked as "disabled"
- Completes in <800ms

---

## Logging & Diagnostics

### Serial Log Capture
```bash
# Capture 5 minutes of logs to file
pio device monitor -b 115200 > v2_node30_logs_$(date +%Y%m%d_%H%M%S).txt
```

**Save logs for:**
- Boot sequence (first 10 seconds)
- Mesh peer discovery (if multi-device)
- Command execution (if tested)
- Any errors or warnings

---

## Summary Checklist

| Phase | Test | Status | Notes |
|-------|------|--------|-------|
| 1.1 | Boot without hang | ⬜ | |
| 1.2 | OLED display | ⬜ | |
| 2.1 | 10min uptime | ⬜ | |
| 2.2 | GPIO 21 conflict resolved | ⬜ | |
| 3.1 | BLE connection & STATUS | ⬜ | |
| 3.2 | Peer discovery (if 2+ devices) | ⬜ | |
| 3.3 | Command routing (if 2+ devices) | ⬜ | |
| 4.1 | GPS gracefully unavailable | ⬜ | |
| 5.1 | STATUS command output | ⬜ | |
| 5.2 | VSTATUS command output | ⬜ | |

---

## Pass Criteria Summary

✅ **PASS** if:
- Device boots and stays up for 10+ minutes
- OLED displays all pages clearly
- BLE STATUS command works
- Relay 6 is marked unavailable (GPIO 21 conflict resolved)
- GPS shows as unavailable (no NMEA spam)
- (If 2+ devices) Peer discovery works
- (If 2+ devices) Command routing works

❌ **FAIL** if:
- Boot hangs or watchdog resets
- OLED doesn't light up or shows corruption
- BLE doesn't connect
- Relay 6 interferes with VEXT
- GPS NMEA flooding the mesh
- Commands timeout or route incorrectly

---

## Next Steps (if PASS)
1. Commit test results to AGENT_RADIO
2. Mark V2 as "Stable for Basic Mesh"
3. Proceed to Phase 50 (V3/V4 advanced features)

## Next Steps (if FAIL)
1. Capture full serial logs
2. Document exact failure point
3. Post to AGENT_RADIO with [DEBUG ID]: `V2-BOOT` / `V2-OLED` / `V2-BLE` / `V2-MESH`
4. Investigate root cause

---

**Test Duration:** ~30 minutes (phases 1-5)
**Device Required:** Heltec V2 (Node 30) + USB cable for serial
**Optional:** 2nd V2 device for mesh testing, BLE terminal app
**Tester:** AG (Antigravity)
**Reviewed By:** Claude
