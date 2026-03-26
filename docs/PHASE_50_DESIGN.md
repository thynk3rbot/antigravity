# Phase 50: Autonomous Mesh Sovereignty — Complete Design
**Author:** Claude
**Date:** 2026-03-26 14:45
**Status:** DESIGN (Ready for implementation)
**Priority:** SHIP THIS WEEK

---

## Executive Summary

**Goal:** Enable mesh-enabled GPIO control — send commands from anywhere to any device in the swarm.

**Example User Flow:**
```
User (webapp) → "Toggle relay on Device B"
              ↓
Daemon → finds Device B in mesh
       → routes command via LoRa/BLE
       ↓
Device B → executes toggle
         → confirms to daemon
         ↓
Webapp → shows "Relay ON" in real-time
```

**Architecture:** Daemon owns mesh routing. Devices execute commands only.

---

## Layer 1: Device Firmware (Minimal)

### Device Responsibility
- Receive mesh commands via MQTT or LoRa
- Execute GPIO action (toggle, set, read)
- Report result + status
- That's it.

### Device Firmware Change Required

**Add to device firmware:**
```cpp
// In CommandManager or new MeshCommandHandler
void handleMeshCommand(const MeshPayload& payload) {
    // payload = {target_mac, command, pin, action, duration_ms}

    if (payload.target_mac == my_mac) {
        // This command is for me
        GpioPayload gpio = GpioPayload::fromMesh(payload);

        if (!gpio.validate()) {
            replyWithError(payload.cmd_id, "Invalid GPIO");
            return;
        }

        executeGpioCommand(gpio);  // Toggle, set, read
        replyWithSuccess(payload.cmd_id, result);
    }
    // else: ignore (not for me)
}
```

**Device firmware DOES NOT:**
- Route to other devices (daemon does)
- Maintain peer lists (daemon tracks mesh)
- Make routing decisions (daemon decides path)

---

## Layer 2: Daemon — Mesh Router (Intelligence)

### Daemon Responsibility

1. **Peer Discovery** — Track all devices in mesh
2. **Routing** — Find best path to target device
3. **Command Queueing** — Queue commands for offline devices
4. **Retry Logic** — Resend if device doesn't ACK
5. **Conflict Resolution** — Handle simultaneous commands to same device

### Daemon Data Structure

```cpp
// In daemon codebase (Python/C++)

struct MeshPeer {
    string node_id;           // "node-30"
    uint8_t mac[6];           // Device MAC address
    string last_seen;         // ISO timestamp
    int16_t rssi_dbm;         // Signal strength
    string transport;         // "lora" or "ble" or "wifi"
    bool reachable;           // Can reach right now?
    vector<string> neighbors; // Direct peers
};

struct MeshCommand {
    string cmd_id;            // Unique ID
    string from_node;         // Originator
    string to_node;           // Target
    string action;            // "gpio_toggle", "gpio_set", etc.
    int pin;
    int duration_ms;
    string status;            // "pending", "sent", "acked", "completed", "failed"
    int retry_count;
    uint64_t queued_at_ms;    // Timestamp
};

class MeshRouter {
public:
    // Route a command to target device
    bool routeCommand(const MeshCommand& cmd);

    // Find best path to target
    vector<string> findPath(string target_node);

    // Handle device status update (heartbeat, neighbor list)
    void updatePeerStatus(string node_id, PeerStatus status);

    // Handle ACK from device
    void handleCommandAck(string cmd_id, bool success, string result);

    // Retry failed commands (background task)
    void retryFailedCommands();
};
```

### Daemon Mesh Routing Algorithm

**Priority order (choose first available):**
1. Direct path (device has WiFi/BLE to daemon)
2. LoRa direct (daemon ↔ device via LoRa)
3. Multi-hop (daemon → peer1 → device via LoRa mesh)
4. Queue for retry (if offline)

**Implementation:**
```cpp
bool MeshRouter::routeCommand(const MeshCommand& cmd) {
    // 1. Check if target is directly reachable
    if (isPeerOnline(cmd.to_node)) {
        return sendDirect(cmd);  // WiFi or LoRa direct
    }

    // 2. Find multi-hop path
    vector<string> path = findPath(cmd.to_node);
    if (!path.empty()) {
        return sendViaMesh(cmd, path);  // Relay through peers
    }

    // 3. Queue for retry (device is offline)
    queueCommand(cmd);
    return true;  // Will retry when device comes online
}
```

---

## Layer 3: Daemon REST API (User Control)

### REST Endpoints

#### 1. Send Command to Device
```
POST /api/mesh/command
Body: {
    "target_node": "node-30",
    "action": "gpio_toggle",
    "pin": 32,
    "duration_ms": 1000
}
Response: {
    "cmd_id": "cmd_abc123",
    "status": "sent",
    "target_node": "node-30"
}
```

#### 2. Check Command Status
```
GET /api/mesh/command/cmd_abc123
Response: {
    "cmd_id": "cmd_abc123",
    "status": "completed",  // pending, sent, acked, completed, failed
    "result": {"pin": 32, "state": 1},
    "timestamp": "2026-03-26T14:50:00Z"
}
```

#### 3. Get Mesh Topology
```
GET /api/mesh/topology
Response: {
    "peers": [
        {
            "node_id": "node-30",
            "mac": "aa:bb:cc:dd:ee:ff",
            "rssi_dbm": -75,
            "transport": "lora",
            "reachable": true,
            "neighbors": ["node-28", "node-42"]
        },
        ...
    ],
    "graph": "visualization data"
}
```

#### 4. Queue Command for Offline Device
```
POST /api/mesh/command-queue
Body: {
    "target_node": "node-50",  // offline
    "action": "gpio_set",
    "pin": 26,
    "state": 1
}
Response: {
    "cmd_id": "cmd_def456",
    "status": "queued",
    "will_retry_until": "2026-03-26T15:50:00Z"  // 1 hour timeout
}
```

#### 5. Get Device Status
```
GET /api/mesh/node/node-30
Response: {
    "node_id": "node-30",
    "uptime_ms": 612345,
    "battery_mv": 4200,
    "relay_states": [1, 0, 1, 0, 1, 0, 0, 0],
    "last_seen": "2026-03-26T14:50:15Z",
    "pending_commands": 2
}
```

---

## MQTT Contract (Device ↔ Daemon)

### Topics

**Device → Daemon (Status):**
```
device/{node_id}/status
→ {"node_id": "node-30", "uptime_ms": 123456, "battery_mv": 4200, ...}
```

**Device → Daemon (Peer List):**
```
device/{node_id}/peers
→ {"neighbors": ["node-28", "node-42"], "rssi": [-75, -80]}
```

**Daemon → Device (Command):**
```
device/{node_id}/mesh/command
← {"cmd_id": "abc123", "action": "gpio_toggle", "pin": 32, "duration_ms": 1000}
```

**Device → Daemon (ACK):**
```
device/{node_id}/mesh/ack
→ {"cmd_id": "abc123", "status": "ok", "result": {"pin": 32, "state": 1}}
```

---

## Provisioning & Mesh Setup

### Initial Device Onboarding

1. **Device boots** → Generates random node_id if not set
2. **Device connects to daemon** (via BLE/WiFi)
3. **Daemon assigns** `net_secret` (for encryption)
4. **Device stores** in NVS (persistent)
5. **Device broadcasts** "I'm online" with MAC address
6. **Daemon tracks** device in peer list

### Mesh Initialization

On first boot in swarm:
```
Device A: "I'm node-30, my MAC is aa:bb:cc:dd:ee:ff"
Daemon: Records A in peer list
Device B: "I'm node-28, can see node-30"
Daemon: Records B, knows A and B are neighbors
Device C: "I'm node-42, can see node-28"
Daemon: Knows topology: C ↔ B ↔ A
```

---

## Error Handling & Recovery

### Command Timeout
```
- Send command to device
- Wait 2 seconds for ACK
- If no ACK: retry (up to 3 times, exponential backoff)
- If still no ACK: mark command as failed, notify user
```

### Device Offline
```
- Command queued for offline device
- Retry when device comes online
- Timeout after 1 hour
```

### Mesh Fragmentation (Device isolated)
```
- Device unreachable even through multi-hop
- Command goes to queue
- Daemon waits for device to rejoin mesh
- Once online, queue drains
```

### Command Conflicts
```
- Two commands to same device simultaneously
- FIFO queue: first in, first out
- Device executes sequentially
- Daemon tracks completion order
```

---

## Integration Points (What Changes)

### Device Firmware
- ✅ Add `handleMeshCommand()` (AG will implement)
- ✅ Use GpioPayload struct (local model generating now)
- ✅ Reply with ACK on MQTT

### Daemon
- ✅ Add MeshRouter class (Claude implementing)
- ✅ Add REST endpoints for `/api/mesh/*`
- ✅ Add MQTT topic handlers for `device/{id}/mesh/*`
- ✅ Track peer topology in memory

### Webapp
- ✅ Add mesh control panel (send command to any device)
- ✅ Show topology graph (who can reach whom)
- ✅ Command status tracking (real-time ACK)

---

## Implementation Sequence

### Day 1 (Tonight)
1. ✅ AG: Implement device `handleMeshCommand()` (uses GpioPayload from local model)
2. ✅ Claude: Implement daemon MeshRouter class + REST endpoints
3. ✅ Local Model: Generate boilerplate (struct, factory, MQTT parsing)

### Day 2
4. ✅ AG: Test device receiving mesh commands
5. ✅ Claude: Test daemon routing logic (simulator or real devices)
6. ✅ Integrate: Device + Daemon + Local model results

### Day 3
7. ✅ Webapp: Add mesh control panel
8. ✅ Full end-to-end test (user controls pins via mesh)
9. ✅ Ship Phase 50

---

## Success Criteria

**Phase 50 is DONE when:**
- [ ] Device receives and executes mesh command
- [ ] Daemon routes command to device via LoRa/mesh
- [ ] Daemon tracks peer topology in real-time
- [ ] Webapp shows mesh graph + can send commands
- [ ] Commands succeed when device online, queue when offline
- [ ] All 3 variants (V2, V3, V4) work together in mesh
- [ ] User can toggle relay from webapp to any device anywhere

---

## Code Skeleton (Ready to Expand)

**Daemon MeshRouter (pseudocode):**
```cpp
class MeshRouter {
private:
    map<string, MeshPeer> peer_registry;      // node_id → peer info
    queue<MeshCommand> command_queue;         // offline device commands
    map<string, MeshCommand> pending_commands; // waiting for ACK

public:
    bool routeCommand(MeshCommand cmd) {
        // Check direct path
        if (isPeerOnline(cmd.to_node)) {
            return sendDirect(cmd);
        }

        // Check multi-hop path
        auto path = findPath(cmd.to_node);
        if (!path.empty()) {
            return sendViaMesh(cmd, path);
        }

        // Queue for later
        command_queue.push(cmd);
        return true;
    }

    void updatePeerStatus(string node_id, PeerStatus status) {
        peer_registry[node_id] = status;
        drainQueueForOnlineDevices();  // Retry queued commands
    }

    void handleCommandAck(string cmd_id, bool success) {
        pending_commands.erase(cmd_id);
        // Log result
    }
};
```

---

## Next Steps (Claude → Implementation)

1. **Tonight:** Implement MeshRouter skeleton in daemon
2. **Tomorrow morning:** Integrate AG's firmware changes
3. **Tomorrow afternoon:** Full mesh test (2+ devices)
4. **Tomorrow evening:** Webapp integration
5. **Next day:** SHIP

---

**Status:** DESIGN COMPLETE — Ready for implementation
**Owner:** Claude (daemon) + AG (firmware) + Local Model (boilerplate)
**Timeline:** Complete by end of week
