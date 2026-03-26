# Phase 50 Fleet Test Checklist

**Goal:** Validate pure mesh + Phase 50 GPIO_SET protocol end-to-end

**Timeline:** Tonight (2026-03-26 evening)

---

## Pre-Test Setup

- [ ] AG flashes 3+ devices with Phase 50 firmware
  - [ ] V2 variant
  - [ ] V3 variant
  - [ ] V4 variant (if available)
- [ ] MQTT broker running on localhost:1883
- [ ] Daemon ready: `python daemon/run.py --log-level DEBUG`

---

## Test 1: Device Registration

**Goal:** Devices automatically register in daemon

**Steps:**
1. Start daemon, watch logs
2. Power on first device
3. Wait 10-15 seconds for status message

**Success Criteria:**
```
[Peer] node-30 registered: MAC=aa:bb:cc:dd:ee:ff, RSSI=-75dBm, neighbors=[...]
```

**If fails:**
- Check device publishes to `device/{node_id}/status` on MQTT
- Check broker connection (mosquitto running?)
- Check device logs for MQTT errors

---

## Test 2: Topology Discovery

**Goal:** Daemon discovers mesh neighbors

**Steps:**
1. Power on 2nd and 3rd devices
2. Wait 30 seconds
3. Query daemon topology:
   ```bash
   curl http://localhost:8001/api/mesh/topology | jq .
   ```

**Success Criteria:**
```json
{
  "node_count": 3,
  "online_count": 3,
  "peers": [
    {"node_id": "node-30", "neighbors": ["node-28"], ...},
    {"node_id": "node-28", "neighbors": ["node-30", "node-42"], ...},
    {"node_id": "node-42", "neighbors": ["node-28"], ...}
  ]
}
```

**If fails:**
- Devices may not be publishing neighbor lists
- Check MQTT topic: `device/{node_id}/peers`
- Check device radio settings (LoRa, BLE range)

---

## Test 3: Direct Command (Single Hop)

**Goal:** Send command to adjacent device

**Setup:**
- Device A and Device B within radio range
- Device A knows Device B as neighbor

**Steps:**
1. Send command:
   ```bash
   curl -X POST http://localhost:8001/api/mesh/command \
     -H "Content-Type: application/json" \
     -d '{
       "target_node": "node-30",
       "action": "gpio_toggle",
       "pin": 32,
       "duration_ms": 1000
     }' | jq .
   ```

2. Watch device logs for command reception
3. Check device executes GPIO toggle
4. Wait for ACK (2-3 seconds)

**Success Criteria:**
```
Device logs: [Mesh] Received command abc123: gpio_toggle pin=32
Device logs: [GPIO] Toggle pin 32: 0→1
Daemon logs: [Ack] abc123: SUCCESS
curl /api/mesh/command/abc123 returns: "status": "completed"
```

**If fails:**
- Device not subscribed to `device/{node_id}/mesh/command` topic
- GPIO toggle not implemented in firmware handleMeshCommand()
- Check pin number validity (0-49 on V2)
- Check ACK publish to `device/{node_id}/mesh/ack`

---

## Test 4: Multi-Hop Command (Mesh Routing)

**Goal:** Command relays through intermediate device

**Setup:**
- Device A — Device B — Device C (chain)
- A can't reach C directly
- B can reach both A and C

**Steps:**
1. Power devices to form chain
2. Verify topology shows C is only reachable through B
3. Send command to C from A:
   ```bash
   curl -X POST http://localhost:8001/api/mesh/command \
     -d '{"target_node": "node-42", "action": "gpio_set", "pin": 26, ...}'
   ```

4. Watch for relay in device B logs
5. Watch for execution in device C logs

**Success Criteria:**
```
Device B logs: [Mesh] Relaying command to node-42
Device C logs: [Mesh] Received command (routed from node-30 via node-28)
Device C logs: [GPIO] Set pin 26: 0
Daemon logs: [Ack] SUCCESS from node-42
```

**If fails:**
- Devices not relaying commands for other targets
- Check ControlPacket routing implementation in firmware
- Check MessageHandler dispatch logic
- Verify all devices understand GPIO_SET (0x07)

---

## Test 5: Offline Device Queue (Future)

**Goal:** Command queues when target offline

**Note:** May not test tonight if limited to 3 devices

**Steps:**
1. Send command to offline device
2. Device comes online
3. Verify command is relayed and executed

---

## Test 6: Concurrent Commands

**Goal:** Multiple commands don't interfere

**Steps:**
1. Send 3 commands to different devices simultaneously
2. Watch all execute in order
3. Verify all ACKs received

**Success Criteria:**
- All 3 commands complete
- No dropped ACKs
- All returned as "completed"

---

## Logging for Analysis

**Before running tests, enable DEBUG logging:**
```bash
python daemon/run.py --log-level DEBUG
```

**Save daemon output:**
```bash
python daemon/run.py --log-level DEBUG > daemon.log 2>&1 &
```

**Device logs:** Check device serial output or device logger

**MQTT traffic (if needed):**
```bash
mosquitto_sub -v -t "device/+" > mqtt.log &
```

---

## Analysis Checklist

After tests, provide:
- [ ] Device firmware version + variants tested
- [ ] Daemon startup logs (first 30 lines)
- [ ] Device registration logs (when device came online)
- [ ] Successful command example (full log sequence)
- [ ] Any error messages or timeouts
- [ ] Topology output (curl /api/mesh/topology)
- [ ] Test timing (how long for each step)

---

## Success Definition

**Minimum for tonight:**
- ✅ 3 devices register in daemon
- ✅ Topology correctly shows neighbors
- ✅ Single-hop command executes (A→B)
- ✅ Device ACKs back to daemon

**Nice to have:**
- ✅ Multi-hop command works (A→B→C)
- ✅ Concurrent commands work
- ✅ Response times < 2 seconds per command

---

## Rollback Plan

If critical failure:
1. Check device firmware built correctly
2. Verify GPIO_SET (0x07) packet type in ControlPacket
3. Verify MessageHandler dispatch includes GPIO handling
4. Check MQTT connectivity on device
5. Try single device first (no mesh routing)

---

**Owner:** AG (firmware test) + Claude (daemon analysis)
**Timeline:** Complete by 2026-03-26 23:59
**Success:** Mesh routing validated, Phase 50 architecture proven
