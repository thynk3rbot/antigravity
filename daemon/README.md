# Magic Pure Mesh Gateway Daemon

**Command Gateway for Phase 50 Autonomous Mesh Sovereignty**

The daemon monitors mesh topology and acts as a command gateway for the webapp. Devices handle all mesh routing via ControlPacket; the daemon provides visibility and command injection points.

## Architecture: Pure Mesh Model

```
Devices mesh with each other (LoRa/BLE/ESP-NOW):
┌─────────────────────────────────┐
│  Device A ←→ Device B ←→ Device C   (ControlPacket routing)
│  All devices understand all commands
│  Devices relay for each other
└─────────────────────────────────┘
          ↑              ↓
          └──────┬───────┘
                 │ MQTT
                 ↓
    ┌────────────────────────┐
    │   MQTT Broker (1883)   │
    └────────────────────────┘
          ↑ status/ACK  ↓ commands
          │             │
    ┌─────┴─────────────┴────┐
    │  Magic Daemon       │
    │  (localhost:8001)      │
    │  - Topology Monitor    │
    │  - Command Gateway     │
    └────────┬───────────────┘
             ↑ HTTP REST
             │
    ┌────────┴────────┐
    │   Webapp        │
    │ (localhost:8000)│
    └─────────────────┘
```

**Key Principle:** Devices own the mesh. Daemon owns the interface.

## Getting Started

### Prerequisites
- Python 3.9+
- MQTT Broker running on localhost:1883 (Mosquitto or equivalent)
- Magic devices flashing Phase 50 firmware with MQTT + ControlPacket support

### Installation & Running

```bash
cd daemon
pip install -r requirements.txt
python run.py --port 8001 --mqtt-broker localhost:1883 --log-level INFO
```

### Device Requirements

Each device must:
1. **Publish status to MQTT** (every 10s):
   ```
   Topic: device/{node_id}/status
   Payload: {
     "node_id": "node-30",
     "uptime_ms": 612345,
     "battery_mv": 4200,
     "rssi_dbm": -75,
     "neighbors": ["node-28", "node-42"]
   }
   ```

2. **Subscribe to command topic**:
   ```
   Topic: device/{node_id}/mesh/command
   Payload: {
     "cmd_id": "abc123...",
     "action": "gpio_toggle",
     "pin": 32,
     "duration_ms": 1000
   }
   ```

3. **Relay commands via ControlPacket** to target device (mesh routing)

4. **Acknowledge back to daemon** (when command completes):
   ```
   Topic: device/{node_id}/mesh/ack
   Payload: {
     "cmd_id": "abc123...",
     "status": "ok",
     "result": {"pin": 32, "state": 1}
   }
   ```

## REST API

### Send Command to Mesh

```bash
curl -X POST http://localhost:8001/api/mesh/command \
  -H "Content-Type: application/json" \
  -d '{
    "target_node": "node-30",
    "action": "gpio_toggle",
    "pin": 32,
    "duration_ms": 1000
  }'
```

Response:
```json
{
  "cmd_id": "abc123def456...",
  "status": "published",
  "target_node": "node-30",
  "message": "Command published to mesh for node-30"
}
```

### Check Command Status

```bash
curl http://localhost:8001/api/mesh/command/abc123def456
```

Response:
```json
{
  "cmd_id": "abc123def456",
  "status": "completed",
  "result": {"pin": 32, "state": 1},
  "error_message": null,
  "timestamp_ms": 1234567890
}
```

### Get Mesh Topology

```bash
curl http://localhost:8001/api/mesh/topology
```

Response:
```json
{
  "node_count": 3,
  "online_count": 3,
  "peers": [
    {
      "node_id": "node-30",
      "mac_address": "aa:bb:cc:dd:ee:ff",
      "rssi_dbm": -75,
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

### Get Device Status

```bash
curl http://localhost:8001/api/mesh/node/node-30
```

Response:
```json
{
  "node_id": "node-30",
  "uptime_ms": 612345,
  "battery_mv": 4200,
  "last_seen": 1234567890000,
  "is_online": true,
  "neighbors": ["node-28", "node-42"]
}
```

### Get Statistics

```bash
curl http://localhost:8001/api/mesh/stats
```

Response:
```json
{
  "own_node_id": "daemon-0",
  "total_peers": 3,
  "online_peers": 3,
  "active_commands": 0,
  "command_history_size": 12
}
```

## Core Components

### MeshTopology (`mesh_router.py`)
- **Passive Monitoring**: Tracks peers from MQTT status updates
- **Command Gateway**: Creates commands for injection into mesh
- **Status Tracking**: Maintains command lifecycle (published → acked → completed)
- **Topology Visibility**: Provides peer registry for REST API

### MeshAPI (`mesh_api.py`)
- REST endpoints for webapp integration
- Command publication to MQTT
- Topology queries
- Device status lookups
- Health checks

### MQTTClient (`mqtt_client.py`)
- Subscribes to device status topics
- Publishes commands for devices to relay
- Processes device ACKs
- Graceful connect/disconnect

### Main Server (`main.py`)
- Orchestrates components
- Background health loop
- Graceful shutdown
- CORS support for webapp

## How It Works: Example Flow

**User wants Device A to toggle relay on Device C (via webapp):**

1. **Webapp** calls: `POST /api/mesh/command` with target="node-C", action="gpio_toggle"

2. **Daemon** publishes to MQTT: `device/node-C/mesh/command` with command payload

3. **Devices mesh the command**:
   - Device A receives command (subscribed to `/mesh/command`)
   - Device A checks if target is Device C
   - If Device C is not reachable, Device A relays via Device B (has Device C as neighbor)
   - Device B forwards to Device C

4. **Device C** executes GPIO toggle on pin 32

5. **Device C** publishes ACK: `device/node-C/mesh/ack` with result

6. **Daemon** receives ACK, marks command as completed

7. **Webapp** polls `/api/mesh/command/{cmd_id}` → sees "completed" status

## Logging

Example output:
```
[Daemon] Initializing Magic Daemon (Pure Mesh Gateway)...
[Daemon] Connecting to MQTT broker at localhost:1883...
[Daemon] Initialization complete! (Pure Mesh Mode)
[Peer] node-30 registered: MAC=aa:bb:cc:dd:ee:ff, RSSI=-75dBm, neighbors=['node-28']
[Peer] node-28 registered: MAC=11:22:33:44:55:66, RSSI=-82dBm, neighbors=['node-30', 'node-42']
[Cmd] abc123 published to MQTT for node-30: gpio_toggle pin=32
[Ack] abc123: SUCCESS
[Health] Peers: 3 total, 3 online | Commands: 0 active, 5 history
```

## Troubleshooting

### Daemon won't start
```bash
python run.py --log-level DEBUG
# Check: Python 3.9+? MQTT broker running on 1883?
```

### Devices not registering
```bash
# Verify MQTT broker receiving status:
mosquitto_sub -h localhost -t "device/+/status"
# Should see: device/node-30/status messages every 10s
```

### Commands not executing
1. Verify device subscribed to `device/{node_id}/mesh/command`
2. Check device logs for incoming command messages
3. Verify ControlPacket GPIO_SET is implemented in firmware
4. Check ACK topic: `device/{node_id}/mesh/ack`

### Commands ACK'd but not executed
- Device received command but execution failed
- Check device logs for GPIO errors
- Verify pin is valid (0-49 on V2, different on other boards)

## Future Enhancements

- [ ] SQLite persistence for command history
- [ ] JWT authentication for REST API
- [ ] Device grouping (send to multiple devices)
- [ ] Command scheduling (run at specific time)
- [ ] Mesh visualization (interactive topology graph)
- [ ] Performance metrics (latency, throughput)
- [ ] OTA firmware update coordination

---

**Architecture:** Pure Mesh Gateway
**Status:** Phase 50 Production Ready
**Owner:** Claude (daemon) + AG (firmware)
