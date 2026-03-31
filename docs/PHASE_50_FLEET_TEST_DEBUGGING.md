# Phase 50 Fleet Test — Debugging Companion

**Purpose:** Rapid issue identification and triage when fleet test runs tonight

**Audience:** Claude analyzing daemon logs + device logs in real-time

---

## Pre-Test Verification (5 minutes)

Run these checks before AG flashes devices:

### 1. Daemon Starts Without Errors

```bash
# Terminal 1: Start daemon with DEBUG logging
cd daemon
python run.py --mqtt-broker localhost:1883 --log-level DEBUG > test_daemon.log 2>&1 &

# Expected output (within 3 seconds):
# [Daemon] Initializing Magic Daemon (Pure Mesh Gateway)...
# [Daemon] Connecting to MQTT broker at localhost:1883...
# [Daemon] Initialization complete! (Pure Mesh Mode)
```

**If fails:** Check logs for:
- `ModuleNotFoundError` → Python path issue, verify `daemon/run.py` is entry point
- `ConnectionRefusedError` → MQTT broker not running, start mosquitto
- Import errors → Missing dependencies, run `pip install -r daemon/requirements.txt`

### 2. MQTT Broker Running

```bash
# Check if mosquitto listening on 1883
netstat -an | grep 1883

# Or try to connect
python -c "import paho.mqtt.client as mqtt; c = mqtt.Client(); c.connect('localhost', 1883, 60); print('OK')"
```

**If fails:**
- Windows: `mosquitto -v` (start in separate terminal)
- Linux: `mosquitto -v` or `service mosquitto start`
- Verify port is 1883 (not already in use)

### 3. Daemon REST API Responds

```bash
curl -s http://localhost:8001/health | jq .

# Expected:
# {
#   "status": "healthy",
#   "service": "Magic Pure Mesh Gateway",
#   "peers": 0,
#   "model": "pure-mesh"
# }
```

**If fails:**
- Daemon crashed silently → check `test_daemon.log`
- Port 8001 already in use → change with `--port 8002`
- FastAPI failed to initialize → check imports in `mesh_api.py`

---

## During Fleet Test — Log Collection

### Daemon Log (`test_daemon.log`)

**What to watch for:**
```
✅ Registration:
[Peer] node-30 registered: MAC=aa:bb:cc:dd:ee:ff, RSSI=-75dBm, neighbors=[...]
[Peer] node-28 registered: MAC=11:22:33:44:55:66, RSSI=-82dBm, neighbors=[...]

✅ Command:
[Command] cmd_id=abc123 published to MQTT
[Ack] abc123: SUCCESS

❌ Issues:
[Error] MQTT publish failed: Connection lost
[Warning] MAC aa:bb:cc:dd:ee:ff changed node_id: node-30 → node-31
[Error] Peer node-30 not in topology
```

### Device Logs (via serial monitor)

**What to watch for:**
```
✅ Boot:
[Mesh] Initialized
[MQTT] Connected to broker
[Status] Publishing status...

✅ Command received:
[Mesh] Received command abc123: gpio_toggle pin=32
[GPIO] Toggle pin 32: 0→1
[Status] Publishing ACK...

❌ Issues:
[Error] Failed to publish to MQTT
[Warning] Unknown command type 0x07
[Error] Invalid target MAC
```

---

## Issue Diagnosis Matrix

### Issue: Devices don't register

**Symptoms:** `curl /api/mesh/topology` shows `"node_count": 0`

**Root causes (check in order):**

1. **Devices not publishing status**
   - Device logs show: No `[Status] Publishing status...` message
   - Fix: Verify devices have MQTT_BROKER set correctly in firmware config
   - Check: Device successfully connected to WiFi? Check device logs for WiFi connect message

2. **MQTT topic mismatch**
   - Check device logs: `[MQTT] Publishing to device/{node_id}/status`
   - Check daemon logs: `[MQTT] Subscribed to device/+/status`
   - If topic shows `device/{something}/status` but daemon doesn't see it:
     - Test with `mosquitto_sub -t 'device/+/status' -v` in separate terminal
     - If no messages appear, device is publishing to wrong topic

3. **MQTT broker connection issue**
   - Device logs: `[Error] MQTT connect failed`
   - Check: MQTT_BROKER_IP and MQTT_BROKER_PORT in device firmware match daemon settings
   - Test: `mosquitto_pub -t test/topic -m '{"test":1}'` from host, check if device sees it
   - If device doesn't see published messages, networking issue (WiFi, firewall, broker)

4. **JSON parsing error**
   - Daemon logs: `[Error] Failed to parse device status JSON`
   - Device logs show publish succeeds but no registration in daemon
   - Check: Device status payload is valid JSON (device: check JSON formatting in code)

**Quick fix checklist:**
- [ ] Device has WiFi connectivity (ping device, check logs)
- [ ] Device MQTT_BROKER matches daemon setting (localhost:1883)
- [ ] Daemon subscribed to device/+/status (check daemon initialization logs)
- [ ] MQTT broker actually running and listening (netstat or mosquitto_sub test)
- [ ] Device status JSON is valid (copy paste device payload, validate)

---

### Issue: Device registers but doesn't respond to commands

**Symptoms:**
- `curl /api/mesh/topology` shows device listed
- `curl /api/mesh/command` returns status "published"
- Device log shows no `[Mesh] Received command` message

**Root causes:**

1. **Device not subscribed to command topic**
   - Expected: Device subscribes to `device/{node_id}/mesh/command`
   - Check device logs: Look for `[MQTT] Subscribed to...` messages
   - If missing, device firmware not subscribing to command topic
   - Fix: Verify firmware has MQTT subscription to `device/{node_id}/mesh/command`

2. **Command published to wrong MQTT topic**
   - Daemon publishes to: `device/{node_id}/mesh/command`
   - Device listens on: `device/{node_id}/mesh/command`
   - Check daemon logs: `[Command] ... published to MQTT`
   - Verify topic matches device subscription
   - Test: `mosquitto_sub -t 'device/+/mesh/command' -v` and send command, verify message appears

3. **MessageHandler not dispatching GPIO_SET**
   - Device receives MQTT message but ignores it
   - Device logs show: Command appears in broker but not in device logs
   - Check device firmware: MessageHandler.cpp must have case for `PacketType::GPIO_SET (0x07)`
   - Check: Command payload JSON matches what firmware expects

4. **ControlPacket addressed to wrong node**
   - Device receives command for different device_id
   - Device logs: `[Mesh] Received command but not for me (target=...)`
   - Check: node_id in firmware matches what daemon shows in topology

**Quick fix checklist:**
- [ ] Device subscribed to `device/{node_id}/mesh/command` (check device logs)
- [ ] Command published to correct topic (mosquitto_sub test)
- [ ] Device receives the MQTT message (check device logs for JSON parse)
- [ ] MessageHandler dispatches GPIO_SET (check firmware code)
- [ ] Command target_node matches device node_id (check daemon logs)

---

### Issue: Device executes but no ACK received

**Symptoms:**
- Device logs show: `[GPIO] Toggle pin 32: 0→1`
- Daemon logs show: No `[Ack] ...` message within 5 seconds
- `curl /api/mesh/command/{cmd_id}` shows status "pending" (not "completed")

**Root causes:**

1. **Device not publishing ACK**
   - Device executed but forgot to ACK
   - Check device logs: Look for `[Status] Publishing ACK...` or similar
   - If missing, device code doesn't call ACK publish
   - Fix: Verify firmware publishes ACK after command execution

2. **ACK published to wrong topic**
   - Expected topic: `device/{node_id}/mesh/ack`
   - Check device logs: Verify ACK topic matches expected
   - Check daemon logs: Look for `[MQTT] Subscribed to device/+/mesh/ack`
   - Test: `mosquitto_sub -t 'device/+/mesh/ack' -v`, execute command, verify ACK appears

3. **ACK JSON format mismatch**
   - Device publishes ACK but daemon can't parse it
   - Daemon logs: `[Error] Failed to parse ACK JSON`
   - Compare device ACK format to what daemon expects
   - Expected format: `{"cmd_id": "abc123", "success": true, ...}`

4. **MQTT subscription missing**
   - Daemon not subscribed to ACK topic
   - Check daemon logs: `[MQTT] Subscribed to device/+/mesh/ack`
   - If missing, daemon initialization failed for that subscription
   - Restart daemon with fresh subscription attempt

**Quick fix checklist:**
- [ ] Device publishes ACK after execution (check device logs)
- [ ] ACK topic is `device/{node_id}/mesh/ack` (mosquitto_sub test)
- [ ] ACK JSON includes cmd_id field (test with mosquitto_pub)
- [ ] Daemon subscribed to mesh/ack topic (daemon logs)
- [ ] ACK received within 5 seconds (check timing, not a timeout)

---

### Issue: Multi-hop command not working (if 3+ devices)

**Symptoms:**
- Single-hop commands work (A→B direct)
- Multi-hop fails (A→C via B)
- Device C shows no command received

**Important:** In pure mesh, devices route via **ControlPacket**, not MQTT. Daemon doesn't do multi-hop routing.

**Root causes:**

1. **Device B not relaying ControlPackets**
   - Expected: Device B intercepts ControlPacket for device C, forwards via LoRa/BLE/ESP-NOW
   - Check: Device B firmware has relay logic (mesh coordinator should handle this)
   - Check device B logs: Look for `[Relay] Forwarding packet to device C`
   - If missing, device B routing not working

2. **Device topology not discovered**
   - MQTT status includes `neighbors_mac` field
   - If device B doesn't list device C as neighbor, routing fails
   - Check daemon logs: Device B's neighbor list in topology
   - If empty, devices not discovering each other (LoRa/BLE range issue?)

3. **ControlPacket addressing wrong**
   - Device C expects ControlPacket with target_mac matching its MAC
   - Device B routing may be using node_id instead of MAC (Phase 50.1 issue)
   - Check firmware: ControlPacket.target field type (should be compatible with device addressing)

4. **Route finding broken**
   - Daemon can't determine path from A→C via B
   - In pure mesh, daemon doesn't route; devices do
   - But daemon should show correct neighbor topology for visualization
   - Check: `curl /api/mesh/topology` shows device B has device C as neighbor

**Quick fix checklist:**
- [ ] Devices A, B, C all registered (check topology)
- [ ] Device B has device C in neighbor list (check topology)
- [ ] Device B has relay logic enabled (check firmware)
- [ ] ControlPacket addressed correctly (check device logs during relay)
- [ ] LoRa/BLE range adequate (try moving devices closer)

---

## Log Analysis Commands

### Extract device registrations
```bash
grep "\[Peer\]" test_daemon.log | head -20
```

### Extract command publications
```bash
grep "\[Command\]" test_daemon.log | grep "published"
```

### Extract ACKs
```bash
grep "\[Ack\]" test_daemon.log
```

### Find errors in daemon log
```bash
grep -i "\[error\]" test_daemon.log
```

### Count unique devices registered
```bash
grep "\[Peer\]" test_daemon.log | grep "registered" | awk '{print $3}' | sort -u | wc -l
```

### Track command lifecycle
```bash
grep "cmd_id=abc123" test_daemon.log  # Replace with actual cmd_id
```

---

## Success Checklist (Post-Test)

Copy-paste this checklist and fill in as test progresses:

```
DEVICE REGISTRATION (5 min expected)
- [ ] Device node-30 registered in daemon log
- [ ] Device node-28 registered in daemon log
- [ ] Device node-42 registered (if 3 devices)
- [ ] All devices show in curl /api/mesh/topology

TOPOLOGY DISCOVERY (10 min expected)
- [ ] Device neighbors visible in topology output
- [ ] Neighbor count matches actual mesh
- [ ] Topology graph accurate

COMMAND EXECUTION (10 min expected)
- [ ] Command published to device node-30 via API
- [ ] Device node-30 receives command in logs
- [ ] Device executes GPIO_TOGGLE
- [ ] Device publishes ACK

ACK RECEPTION (30 seconds expected)
- [ ] Daemon receives ACK
- [ ] Command status changes to "completed"
- [ ] curl /api/mesh/command/{cmd_id} shows "completed"

OPTIONAL: MULTI-HOP (10 min if 3+ devices)
- [ ] Command to device node-42 via relay node-28
- [ ] Device node-42 receives and executes
- [ ] ACK returns through relay
```

---

## Emergency Diagnostics

If multiple issues or test stalled:

### 1. Check all three layers

**Daemon layer:**
```bash
curl http://localhost:8001/health
curl http://localhost:8001/api/mesh/topology | jq .
```

**MQTT layer:**
```bash
mosquitto_sub -t 'device/+/status' -v   # Watch for device status
mosquitto_sub -t 'device/+/mesh/ack' -v # Watch for ACKs
mosquitto_pub -t test/ping -m '1'       # Test broker connectivity
```

**Device layer:**
```
Check device serial monitor for:
- WiFi connect success?
- MQTT connect success?
- Status publish output?
- Command receive output?
```

### 2. Isolate the problem

**Is it a registration problem?**
```bash
# Wait 10 seconds after device boot, then check topology
sleep 10
curl http://localhost:8001/api/mesh/topology | jq '.node_count'
```

**Is it a command delivery problem?**
```bash
# Send command and watch MQTT topic
mosquitto_sub -t 'device/+/mesh/command' -v &  # Run in background
curl -X POST http://localhost:8001/api/mesh/command \
  -H "Content-Type: application/json" \
  -d '{"target_node": "node-30", "action": "gpio_toggle", "pin": 32}'
# Does message appear in mosquitto_sub output?
```

**Is it a device execution problem?**
```
Manually publish command via MQTT and watch device:
mosquitto_pub -t device/node-30/mesh/command -m '{"cmd_id":"test1","action":"gpio_toggle","pin":32}'
Check device logs: does it receive and execute?
```

### 3. Collect diagnostic bundle

If stuck, gather these for debugging:
```bash
# Daemon logs
cp test_daemon.log test_results_daemon.log

# Device serial logs (copy from terminal or device output)
# Device dmesg/syslog (if available)

# Topology snapshot
curl http://localhost:8001/api/mesh/topology > test_topology.json

# Command history
curl http://localhost:8001/api/mesh/stats > test_stats.json
```

---

## Escalation Path

If unable to resolve:

1. **Verify assumptions**: Re-read test quick start, ensure all preconditions met
2. **Check firmware**: Ask AG if Phase 50 build was correct (MQTT support, GPIO_SET dispatch)
3. **Isolate to MQTT**: Test MQTT directly (mosquitto_pub/sub) independent of daemon
4. **Isolate to daemon**: Run daemon against mock device (publish test MQTT messages)
5. **Check network**: Ping device, verify WiFi connectivity, check firewall

---

**Ready for test. Stand by for results. 🚀**

