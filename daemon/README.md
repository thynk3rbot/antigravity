# LoRaLink Phase 50 Daemon

**Central Intelligence Layer for Autonomous Mesh Sovereignty**

The daemon is the intelligence hub that coordinates mesh-enabled GPIO control across LoRaLink devices. It handles device discovery, intelligent routing, command queueing, and real-time synchronization between the webapp and hardware swarm.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│               Webapp (localhost:8000)                   │
│          (DaemonClient calls REST API)                  │
└──────────────────┬──────────────────────────────────────┘
                   │
                   │ HTTP (REST API)
                   ↓
┌─────────────────────────────────────────────────────────┐
│    LoRaLink Daemon (localhost:8001)                     │
├─────────────────────────────────────────────────────────┤
│ • MeshRouter: peer discovery, routing, queueing        │
│ • FastAPI: REST endpoints for mesh control             │
│ • MQTT Client: device status, telemetry, ACKs          │
│ • Background: retry loop, health checks                │
└──────┬──────────────────────────────────┬───────────────┘
       │                                   │
       │ MQTT (1883)                       │ (future) BLE/Serial/LoRa
       ↓                                   ↓
┌──────────────────────────┐   ┌─────────────────────────┐
│   MQTT Broker (Mosquitto)│   │  LoRaLink Devices       │
│   (localhost:1883)       │   │  (V2, V3, V4 boards)    │
└──────────────────────────┘   └─────────────────────────┘
```

## Getting Started

### Prerequisites
- Python 3.9+
- MQTT Broker (Mosquitto or equivalent) running on localhost:1883
- LoRaLink devices flashing v0.0.11+ firmware with MQTT support

### Installation

```bash
cd daemon
pip install -r requirements.txt
```

### Running

**Windows:**
```batch
start_daemon.bat [port] [mqtt_broker] [log_level]
```

**Linux/Mac:**
```bash
python src/main.py --port 8001 --mqtt-broker localhost:1883 --log-level INFO
```

### Example: Send Command to Device

```bash
# Send GPIO toggle command to device "node-30"
curl -X POST http://localhost:8001/api/mesh/command \
  -H "Content-Type: application/json" \
  -d '{
    "target_node": "node-30",
    "action": "gpio_toggle",
    "pin": 32,
    "duration_ms": 1000
  }'

# Response:
# {
#   "cmd_id": "abc123def456...",
#   "status": "sent",
#   "target_node": "node-30",
#   "message": "Command sent via LoRa"
# }
```

## REST API Endpoints

### Core Endpoints

#### Send Command
```
POST /api/mesh/command
Body: {
  "target_node": "node-30",
  "action": "gpio_toggle|gpio_set|gpio_read",
  "pin": 32,
  "duration_ms": 1000  # optional
}
Response: {
  "cmd_id": "...",
  "status": "sent|queued",
  "target_node": "...",
  "message": "..."
}
```

#### Check Command Status
```
GET /api/mesh/command/{cmd_id}
Response: {
  "cmd_id": "...",
  "status": "pending|sent|acked|completed|failed",
  "result": {...},
  "error_message": null,
  "timestamp_ms": 1234567890
}
```

#### Get Mesh Topology
```
GET /api/mesh/topology
Response: {
  "node_count": 3,
  "peers": [
    {
      "node_id": "node-30",
      "mac_address": "aa:bb:cc:dd:ee:ff",
      "rssi_dbm": -75,
      "transport": "lora",
      "reachable": true,
      "neighbors": ["node-28", "node-42"],
      "battery_mv": 4200,
      "uptime_ms": 612345
    },
    ...
  ],
  "own_node_id": "daemon-0"
}
```

#### Queue Command for Offline Device
```
POST /api/mesh/command-queue
Body: (same as /api/mesh/command)
Response: {
  "cmd_id": "...",
  "status": "queued",
  "target_node": "...",
  "message": "Command queued (device offline, retry in 30000ms)"
}
```

#### Get Device Status
```
GET /api/mesh/node/{node_id}
Response: {
  "node_id": "node-30",
  "uptime_ms": 612345,
  "battery_mv": 4200,
  "relay_states": null,  # TODO
  "last_seen": 1234567890000,
  "pending_commands": 2,
  "is_online": true
}
```

#### Get Router Statistics
```
GET /api/mesh/stats
Response: {
  "own_node_id": "daemon-0",
  "peer_count": 3,
  "pending_commands": 2,
  "queued_commands": 1,
  "command_history_size": 45
}
```

#### Health Check
```
GET /health
Response: {
  "status": "healthy",
  "service": "LoRaLink Daemon",
  "peers": 3
}
```

## MQTT Topic Contract

Devices communicate with daemon via MQTT following this schema:

### Device → Daemon (Inbound)

**Status Update** (every 10 seconds)
```
Topic: device/{node_id}/status
Payload: {
  "node_id": "node-30",
  "uptime_ms": 612345,
  "battery_mv": 4200,
  "relay_states": [1, 0, 1, 0, 1, 0, 0, 0],
  "temperature_c": 25.5,
  "neighbors": ["node-28", "node-42"],
  "rssi": [-75, -80]
}
```

**Peer List** (mesh neighbors)
```
Topic: device/{node_id}/peers
Payload: {
  "neighbors": ["node-28", "node-42"],
  "rssi": [-75, -80]
}
```

**Command Acknowledgment** (in response to mesh/command)
```
Topic: device/{node_id}/mesh/ack
Payload: {
  "cmd_id": "abc123def456...",
  "status": "ok|error",
  "result": {
    "pin": 32,
    "state": 1,
    "duration_ms": 1000
  }
}
```

### Daemon → Device (Outbound)

**Command** (routed via daemon)
```
Topic: device/{node_id}/mesh/command
Payload: {
  "cmd_id": "abc123def456...",
  "action": "gpio_toggle",
  "pin": 32,
  "duration_ms": 1000
}
```

## Core Components

### MeshRouter (`mesh_router.py`)

Intelligent routing engine with:
- **Peer Registry**: Tracks all devices (MAC, signal, neighbors, reachability)
- **Path Finding**: Determines best route (direct/multi-hop) to target
- **Command Routing**: Sends command via optimal path or queues if offline
- **Queueing**: Holds commands for offline devices, retries when online
- **Retry Logic**: Exponential backoff (max 3 retries, 2s initial)
- **Conflict Resolution**: FIFO queue for simultaneous commands

### MeshAPI (`mesh_api.py`)

FastAPI endpoints exposing MeshRouter functionality:
- Command submission and status tracking
- Topology visualization
- Device status queries
- Queue management

### MQTTClient (`mqtt_client.py`)

Handles all MQTT communication:
- Subscribes to device topics (`device/+/status`, `device/+/peers`, `device/+/mesh/ack`)
- Processes status updates → updates peer registry
- Processes ACKs → marks commands complete
- Publishes commands to devices

### Main Server (`main.py`)

Orchestrates everything:
- Initializes MeshRouter, MQTT, FastAPI
- Runs background tasks (retry loop every 30s, health check every 60s)
- Handles graceful shutdown
- Logs diagnostics

## Background Tasks

### Retry Loop (30s interval)
- Iterates through command queue
- Attempts to resend queued commands for devices now online
- Respects max retry count (default 3)

### Health Loop (60s interval)
- Logs peer count, pending/queued command counts
- Marks peers stale if no status update in 2 minutes
- Prunes old entries for memory efficiency

## Logging

Log levels: DEBUG, INFO, WARNING, ERROR

Example output:
```
[2026-03-26 14:50:15] daemon.mesh_router - INFO - [Peer] node-30 registered: MAC=aa:bb:cc:dd:ee:ff, RSSI=-75dBm, transport=lora, neighbors=['node-28', 'node-42']
[2026-03-26 14:50:20] daemon.mesh_router - INFO - [Cmd] Routing abc123: gpio_toggle to node-30
[2026-03-26 14:50:21] daemon.mesh_router - INFO - [Route] Direct path to node-30 via lora
[2026-03-26 14:50:22] daemon.mqtt_client - INFO - [Ack] abc123: ok
[2026-03-26 14:50:22] daemon.mesh_router - INFO - [Ack] abc123 completed: {'pin': 32, 'state': 1}
```

## Testing

### Unit Tests (coming soon)
```bash
cd tests
pytest test_mesh_router.py
pytest test_mesh_api.py
```

### Manual Integration Test

1. Start daemon: `python src/main.py`
2. Ensure MQTT broker is running
3. Flash a device with v0.0.11+ firmware
4. Device should register in mesh within 30 seconds
5. Verify in logs: `[Peer] node-XX registered`
6. Send command via curl (see examples above)
7. Watch device respond and ACK appear in logs

## Known Limitations

- **No persistent storage**: Command history lost on daemon restart (could add SQLite)
- **No authentication**: REST API is open (add JWT/OAuth for production)
- **No multi-daemon coordination**: Assumes single daemon (could add clustering)
- **No device authentication**: Trusts all MQTT messages (could add signing)
- **Relay states**: Not yet pulled from device telemetry (TODO)

## Future Enhancements

- [ ] SQLite persistence for command history
- [ ] JWT authentication for REST API
- [ ] Multi-hop path visualization
- [ ] Command scheduling (run at specific time)
- [ ] Device grouping (send to multiple devices)
- [ ] OTA firmware update coordination
- [ ] Performance metrics (avg latency, throughput)

## Troubleshooting

### Daemon won't start
```bash
python src/main.py --log-level DEBUG
# Check: Python 3.9+? Dependencies installed? Port 8001 free?
```

### Devices not registering
```bash
# Verify MQTT broker running:
mosquitto_sub -t "device/+/status"
# Should see device status messages arriving
```

### Commands not being acknowledged
```bash
# Check device logs for incoming mesh/command messages
# Verify device firmware includes handleMeshCommand() (Phase 50 requirement)
# Check MQTT ACK topic: device/{node_id}/mesh/ack
```

### High latency
```bash
# Check RSSI values: devices should be -30 to -90 dBm
# Look for multi-hop paths (slower than direct)
# Check MQTT broker load
```

---

**Status:** Phase 50 Implementation In Progress
**Owner:** Claude Agent (daemon) + AG (firmware validation)
**Timeline:** Ship by end of week
