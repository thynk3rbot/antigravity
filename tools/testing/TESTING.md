# Magic Integration Testing Framework

Complete command validation and wiring verification for firmware + PC-based spoofing (no daemon required).

## Quick Start

### 1. Flash Firmware
```bash
pio run -t upload -e heltec_v4
```

### 2. Run Critical Commands (Fast)
```bash
# HTTP transport
python nightly_test.py --transport http --critical-only

# MQTT transport (requires MQTT broker running)
python nightly_test.py --transport mqtt --broker-host localhost --critical-only
```

### 3. Run Full Integration Suite (Comprehensive)
```bash
# Test all commands over both transports
python testing/integration_test.py \
  --ip 172.16.0.27 \
  --broker localhost \
  --transports http,mqtt
```

### 4. Run Endurance Test (Overnight)
```bash
# 5 cycles, 60-second delays between cycles
python testing/overdrive.py \
  --ip 172.16.0.27 \
  --cycles 5 \
  --delay 60 \
  --critical-only
```

## Test Modes

### `nightly_test.py` — Regression Testing
- Fast regression suite
- Per-command latency tracking
- Mesh-aware response polling (12s timeout)
- Outputs: Rich console table, pass/fail summary

**Usage:**
```bash
# HTTP (local device)
python nightly_test.py --ip 172.16.0.27 --transport http

# MQTT (requires broker)
python nightly_test.py --transport mqtt --broker-host 192.168.1.100

# Critical commands only
python nightly_test.py --critical-only

# Specific commands
python nightly_test.py --commands STATUS,GPIO,READ
```

### `overdrive.py` — Endurance Testing
- Multiple cycles of command suite
- Per-cycle reporting
- Critical command tracking (marked with ★)
- JSON log + auto-generated To-Do list for failures
- HTML dashboard generation

**Usage:**
```bash
# 10 cycles with 30-second delays
python testing/overdrive.py --cycles 10 --delay 30

# Critical commands only
python testing/overdrive.py --cycles 5 --critical-only
```

### `integration_test.py` — Unified Testing
- Single-run comprehensive test
- HTTP + MQTT in one command
- Per-transport coverage reporting
- Wiring verification (checks if all handlers implemented)
- JSON + console summary

**Usage:**
```bash
# All commands, both transports
python testing/integration_test.py --transports http,mqtt

# Critical only
python testing/integration_test.py --critical-only

# Custom timeout
python testing/integration_test.py --timeout 10.0
```

## Configuration

All test harnesses use `tools/testing/commands.yaml`:

```yaml
commands:
  STATUS:
    description: "System status"
    transports: [http, mqtt]
    critical: true
    wiring_status: implemented
    args: ""
```

**Key fields:**
- `critical`: True for core commands (test these first)
- `wiring_status`: `implemented`, `partial`, `not_implemented`
- `transports`: Which transports support this command
- `expected_response`: JSON schema or response format hints

## MQTT Setup

The test harnesses include built-in MQTT support (requires `paho-mqtt`):

```bash
pip install paho-mqtt
```

**Default settings:**
- Broker: `localhost:1883`
- Command topic: `magic/cmd`
- Response topic: `magic/+/response`

**To use custom broker:**
```bash
python nightly_test.py --transport mqtt --broker-host 192.168.1.100 --broker-port 1883
```

## Custom MQTT Client

For advanced use cases, edit `mqtt_spoof.py`:
- Standalone MQTT message generator
- Can spoof device responses for testing
- Demo included (run: `python mqtt_spoof.py`)

## Interpreting Results

### Pass/Fail Status
- **PASS**: Command executed successfully
- **FAIL**: Command sent but got unexpected response
- **ERROR**: Connection/timeout issue

### Wiring Status
- **implemented**: Handler is wired up, all codepaths should work
- **partial**: Some codepaths work, others may not
- **not_implemented**: Handler not yet implemented

### Coverage
- **Critical only**: Fast 6-7 command tests (~30-60 seconds)
- **Full suite**: All 24 commands (~2-3 minutes per transport)

## Output Artifacts

### Logs Directory
```
tools/testing/logs/
├── overdrive_20260331_054200.json    # Raw test data (JSON)
├── TODO_20260331.md                  # Auto-generated failure tasks
├── integration_test_20260331_054300.json
└── report_20260331.html              # Dashboard (if generated)
```

## Troubleshooting

### "Timeout after 5s"
- Device not responding. Check:
  - Device is powered and WiFi connected
  - Firmware is flashed (run `pio run -t upload`)
  - IP address is correct

### "MQTT not connected"
- Broker not running or unreachable. Check:
  - Broker is running (`mosquitto` or other)
  - Broker address is correct
  - Firewall allows port 1883

### "Mesh Timeout (Gateway OK, Node Idle)"
- LoRa mesh node not responding (expected if using --target flag with offline node)
- Increase timeout: `python nightly_test.py --timeout 20.0`

## Advanced Usage

### Test with Mesh Target
```bash
# Test commands routed through mesh to specific node
python nightly_test.py --target NODE_001 --transport http
```

### Compare HTTP vs MQTT
```bash
# Test same commands over both transports
python testing/integration_test.py \
  --ip 172.16.0.27 \
  --broker localhost \
  --transports http,mqtt \
  --critical-only
```

### Full Regression After Code Change
```bash
# 1. Flash
pio run -t upload -e heltec_v4

# 2. Quick check
python nightly_test.py --critical-only

# 3. Full validation
python testing/integration_test.py

# 4. Overnight soak
python testing/overdrive.py --cycles 20 --delay 30
```

## Command Reference

### 24 Total Commands
**Critical (6):** STATUS, GPIO, READ, RELAY, SCHED, REBOOT
**Important (5):** NODES, MSG, SETNAME, GETCONFIG, HELP
**Config (5):** SETWIFI, SETIP, SETBROKER, SETKEY, FACTORY_RESET
**Utility (8):** VSTATUS, BLINK, GPS, ASK, FORWARD, REPEATER, SLEEP, LIST, LOAD

See `commands.yaml` for full metadata.

## CI/CD Integration

For automated testing:
```bash
#!/bin/bash
# Build and test
pio run -t upload -e heltec_v4
python tools/testing/integration_test.py --critical-only --timeout 10.0

# Check exit code
if [ $? -eq 0 ]; then
  echo "✓ Integration tests passed"
else
  echo "✗ Integration tests failed"
  exit 1
fi
```
