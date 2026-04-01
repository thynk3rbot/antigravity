# Magic v2 - Bench Mode Diagnostic Testing

**Purpose:** Hardware validation and component testing during development and deployment

**Status:** ✅ Fully Integrated — Use for development workflow validation

---

## Overview

Bench Mode is an optional compilation mode that runs comprehensive hardware diagnostics on startup. It validates all major firmware components without requiring full mesh operation.

### When to Use

- **Development:** Verify hardware after code changes
- **Deployment:** Pre-flight check before field deployment
- **Troubleshooting:** Isolate component failures
- **CI/Testing:** Automated validation in test environments

---

## Quick Start

### Enable Bench Mode

Edit `platformio.ini` and add `-D BENCH_MODE` to the desired environment:

```ini
[env:heltec_v2_hub]
platform = espressif32
board = heltec_wifi_lora_32
framework = arduino
lib_deps =
    RadioLib @ 6.4.0
    Adafruit SSD1306 @ 2.5.0
    Adafruit GFX Library @ 1.11.0
    PubSubClient @ 2.8
build_flags =
    -std=c++17
    -fexceptions
    -Wall -Wextra
    -D FIRMWARE_VERSION=\"0.2.0\"
    -D MAGIC_V2
    -D ROLE_HUB
    -D RADIO_SX1276
    -D ARDUINO_HELTEC_WIFI_LORA_32
    -D BENCH_MODE              # ← ADD THIS LINE
```

### Build & Run

Using the development workflow from BUILD.md:

**Terminal 1: Watch build**
```bash
cd magic-v2
platformio run --environment heltec_v2_hub -w
```

**Terminal 2: Serial monitor**
```bash
platformio device monitor --baud 115200
```

### Expected Output

```
╔════════════════════════════════════════╗
║     Magic v2 Bench Mode Suite       ║
╚════════════════════════════════════════╝

════════════════════════════════════════
[TEST 1/6] Radio HAL Initialization
════════════════════════════════════════
  ✓ Radio HAL initialized in 245 ms
    Model: SX1276 (V2)

════════════════════════════════════════
[TEST 2/6] Relay GPIO Control
════════════════════════════════════════
  ✓ Relay GPIO control OK (2 ms)
    States: 0x00, 0xFF, 0x55

════════════════════════════════════════
[TEST 3/6] LoRa TX/RX Loopback
════════════════════════════════════════
  Transmitting: 'BENCH' (5 bytes)
  TX: 5 bytes sent
  RX: 0 bytes received
  RSSI: -120 dBm
  ✓ LoRa TX operational (183 ms)

════════════════════════════════════════
[TEST 4/6] Control Packet Encoding
════════════════════════════════════════
  Telemetry packet size: 14 bytes
  Action packet size: 14 bytes
  ACK packet size: 14 bytes
  ✓ Packet formats valid (1 ms)

════════════════════════════════════════
[TEST 5/6] Packet Deduplication
════════════════════════════════════════
  Hash count: 5
  Test duplicate detection: PASS
  Test unique detection: PASS
  ✓ Deduplication logic valid (0 ms)

════════════════════════════════════════
[TEST 6/6] Mesh Coordinator
════════════════════════════════════════
  Neighbors added: 3
  Next hop to node 3: 2
  ✓ Mesh coordinator working (5 ms)

╔════════════════════════════════════════╗
║         BENCH TEST RESULTS             ║
╚════════════════════════════════════════╝

[1] Radio Init               ✓ PASS (245 ms)
[2] Relay GPIO              ✓ PASS (2 ms)
[3] LoRa TX/RX              ✓ PASS (183 ms)
[4] Packet Formats          ✓ PASS (1 ms)
[5] Deduplication           ✓ PASS (0 ms)
[6] Mesh Coordinator        ✓ PASS (5 ms)

╔════════════════════════════════════════╗
║        ✓ ALL TESTS PASSED              ║
╚════════════════════════════════════════╝

[1/6] Initializing HAL...
  ✓ HAL initialized
[2/6] Initializing transports...
  [LoRaTransport] Initialized
  ✓ Transport initialized
...
```

---

## Test Descriptions

### Test 1: Radio HAL Initialization
- **Purpose:** Verify radio chip (SX1276/SX1262) is accessible
- **Checks:** SPI communication, register readback, model detection
- **Pass Criteria:** Radio responds to initialization commands
- **Expected Duration:** 200-300 ms

### Test 2: Relay GPIO Control
- **Purpose:** Verify all 8 relay channels respond to control
- **Checks:** GPIO output, bitmask state management
- **Pass Criteria:** All states (0x00, 0xFF, 0x55) set correctly
- **Expected Duration:** 1-5 ms

### Test 3: LoRa TX/RX Loopback
- **Purpose:** Verify transmitter operational (RX requires loopback hardware)
- **Checks:** Radio transmission, RSSI reporting
- **Pass Criteria:** TX successful, signal strength reported
- **Expected Duration:** 150-250 ms

### Test 4: Control Packet Encoding
- **Purpose:** Verify packet struct sizes match specification
- **Checks:** Telemetry, Action, ACK packet sizes
- **Pass Criteria:** All packets = 14 bytes
- **Expected Duration:** <5 ms

### Test 5: Packet Deduplication
- **Purpose:** Verify hash-based duplicate detection logic
- **Checks:** Rolling hash buffer, duplicate/unique discrimination
- **Pass Criteria:** Correct duplicate/unique detection
- **Expected Duration:** <5 ms

### Test 6: Mesh Coordinator
- **Purpose:** Verify neighbor tracking and greedy routing
- **Checks:** Neighbor update, next-hop selection
- **Pass Criteria:** Neighbors tracked, routes computed
- **Expected Duration:** 5-10 ms

---

## Development Workflow Integration

### Recommended Cycle

1. **Make code change**
   ```bash
   # Edit file in src/ or lib/
   ```

2. **Enable bench mode** (in platformio.ini)
   ```ini
   -D BENCH_MODE
   ```

3. **Watch build** (Terminal 1)
   ```bash
   platformio run --environment heltec_v2_hub -w
   ```

4. **Monitor output** (Terminal 2)
   ```bash
   platformio device monitor --baud 115200
   ```

5. **Verify all tests pass** before deploying to mesh

### Disabling Bench Mode

Once validated, remove `-D BENCH_MODE` from platformio.ini to skip diagnostics and boot faster (~2 seconds saved).

---

## Troubleshooting

### Test Fails: "Radio HAL initialization failed"

**Cause:** SPI bus not responding

**Debug Steps:**
1. Check SPI pins in `board_config.h` match hardware
2. Verify RadioLib dependency installed: `platformio lib update`
3. Try clean rebuild: `platformio run --environment <target> --target clean`
4. Check radio chip is populated on board

### Test Fails: "Relay GPIO control failed"

**Cause:** GPIO pins not responding

**Debug Steps:**
1. Verify relay GPIO pins in `board_config.h`
2. Check for GPIO conflicts with LoRa SPI pins
3. Ensure board variant matches build flag (`ROLE_HUB` vs `ROLE_NODE`)

### Test Fails: "LoRa TX operational" but RX fails

**Cause:** RX requires loopback (two radios close to each other)

**Expected:** TX passing is sufficient for single-board testing. RX timeout is normal without loopback.

### Test Fails: "Mesh Coordinator working"

**Cause:** Neighbor tracking logic issue

**Debug Steps:**
1. Check mesh_coordinator.cpp neighbor update implementation
2. Verify getNextHop() greedy algorithm (best RSSI)

---

## Performance Metrics

| Test | Min | Typical | Max |
|------|-----|---------|-----|
| Radio Init | 100 ms | 245 ms | 400 ms |
| Relay GPIO | <1 ms | 2 ms | 10 ms |
| LoRa TX/RX | 100 ms | 183 ms | 250 ms |
| Packet Formats | <1 ms | 1 ms | 5 ms |
| Deduplication | <1 ms | 0 ms | 1 ms |
| Mesh Coordinator | 1 ms | 5 ms | 15 ms |
| **Total Suite** | **300 ms** | **436 ms** | **700 ms** |

---

## Code Structure

### Files Involved

- **bench_test.h** — Bench mode interface & declarations
- **bench_test.cpp** — Test implementations (600 LOC)
- **main.cpp** — Integration point (boot sequence)
- **platformio.ini** — Compile-time flag configuration

### Adding New Tests

To add a new diagnostic test:

1. Add method to `BenchTest` class in `bench_test.h`
2. Implement test logic in `bench_test.cpp`
3. Call test in `BenchTest::runAll()` method
4. Increment test count (e.g., "[TEST 7/7]")

Example:
```cpp
// In bench_test.h
static void testNewComponent();

// In bench_test.cpp
void BenchTest::testNewComponent() {
  uint32_t start = millis();
  // ... test implementation ...
  uint32_t duration = millis() - start;
  bool passed = (/* test result */);
  recordResult("New Component", passed, duration, "MESSAGE");
}
```

---

## Related Documentation

- **BUILD.md** — Development workflow and build instructions
- **README.md** — Project overview and quick start
- **lib/README.md** — Architecture and layer descriptions
- **01_planning/spec.md** — Complete technical specification

---

## Summary

Bench Mode provides quick hardware validation following the established development methodology:

✅ **Fast** — Runs in <1 second
✅ **Comprehensive** — Tests all major components
✅ **Integrated** — Part of boot sequence (optional)
✅ **Development-Friendly** — Used in watch-mode workflow

Enable `-D BENCH_MODE` during development, disable for production builds.
