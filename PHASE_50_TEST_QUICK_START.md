# Phase 50 Fleet Test — Quick Start Guide

**Goal:** Validate pure mesh + GPIO_SET protocol end-to-end
**Timeline:** Tonight (2026-03-26)
**Success:** 3+ devices register, commands execute, ACKs return

---

## What You Need

- **Firmware:** Phase 50 (GPIO_SET + MessageHandler dispatch)
- **MQTT Broker:** Running on localhost:1883 (Mosquitto)
- **Daemon:** `python daemon/run.py`
- **Devices:** 3+ Magic boards (V2/V3/V4)

---

## Quick Start

### 1. Start Daemon (5 seconds)
```bash
cd daemon
python run.py --mqtt-broker localhost:1883 --log-level DEBUG > daemon.log 2>&1 &
```

**Watch for:**
```
[Daemon] Initializing Magic Daemon (Pure Mesh Gateway)...
[Daemon] Connecting to MQTT broker at localhost:1883...
[Daemon] Initialization complete! (Pure Mesh Mode)
```

### 2. Flash Devices (5-10 min)
AG flashes 3+ devices with Phase 50 firmware. All targets build-verified ✅

### 3. Verify Registration (30 seconds)
Devices auto-register when they publish MQTT status messages.

**Daemon logs should show:**
```
[Peer] node-30 registered: MAC=aa:bb:cc:dd:ee:ff, RSSI=-75dBm, neighbors=[...]
[Peer] node-28 registered: MAC=11:22:33:44:55:66, RSSI=-82dBm, neighbors=[...]
[Peer] node-42 registered: MAC=...
```

### 4. Check Topology (curl)
```bash
curl http://localhost:8001/api/mesh/topology | jq .
```

**Should show:**
```json
{
  "node_count": 3,
  "online_count": 3,
  "peers": [
    {"node_id": "node-30", "reachable": true, "neighbors": ["node-28"], ...},
    {"node_id": "node-28", "reachable": true, "neighbors": ["node-30", "node-42"], ...},
    {"node_id": "node-42", "reachable": true, "neighbors": ["node-28"], ...}
  ]
}
```

### 5. Send Test Command
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

**Response should include:**
```json
{
  "cmd_id": "abc123def456...",
  "status": "published",
  "target_node": "node-30"
}
```

### 6. Watch Device Execute
Device logs should show:
```
[Mesh] Received command abc123: gpio_toggle pin=32
[GPIO] Toggle pin 32: 0→1
```

### 7. Check ACK
Daemon logs should show:
```
[Ack] abc123: SUCCESS
```

Check status:
```bash
curl http://localhost:8001/api/mesh/command/abc123def456 | jq .
```

Should show:
```json
{
  "cmd_id": "abc123def456",
  "status": "completed",
  "result": {"pin": 32, "state": 1}
}
```

---

## Success Checklist

- [ ] 3+ devices register in daemon logs
- [ ] `curl /api/mesh/topology` shows all devices
- [ ] Command published to MQTT
- [ ] Device receives + executes command
- [ ] Device ACKs back
- [ ] Daemon marks command "completed"
- [ ] `curl /api/mesh/command/{id}` shows "completed" status

---

## Troubleshooting Quick Fixes

| Problem | Check |
|---------|-------|
| No devices register | Device publishes to `device/{node_id}/status`? MQTT broker running? |
| Command not published | Daemon connected to MQTT? No errors in daemon.log? |
| Device not executing | Device subscribed to `device/{node_id}/mesh/command`? Pin valid (0-49)? |
| No ACK | Device publishes to `device/{node_id}/mesh/ack`? Check device logs. |

---

## Logs & Analysis

**Save daemon logs for analysis:**
```bash
python run.py --log-level DEBUG > daemon.log 2>&1 &
```

**Quick analysis after test:**
```bash
python daemon/analyze_test.py daemon.log
```

---

## What to Report

When done, provide:
1. **Device log snippets** (registration, command rx, execution)
2. **Daemon log segments** (startup, registrations, command flow, ACKs)
3. **Topology output** (curl /api/mesh/topology)
4. **Command status** (curl /api/mesh/command/{id})
5. **Any errors** that occurred

---

## Expected Timing

- Device registration: ~10 seconds after boot
- Command publish: < 100ms
- Device execution: ~50-200ms
- ACK return: ~100-300ms
- Total command cycle: < 1 second

---

**You're ready. Go test! 🚀**