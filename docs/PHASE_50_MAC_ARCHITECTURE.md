# Phase 50: MAC-Primary Architecture Roadmap

**Goal:** MAC is the software identifier. node_id is a unique human-friendly alias that survives collisions gracefully.

**Scope:** Full swarm, any number of devices, no single point of failure for naming.

---

## Current Design (Tonight's Test)

```
Limitation:
  node_id = "node-30"  ← Primary (human-readable)
  mac = "aa:bb:cc:dd:ee:ff"  ← Metadata only

Problem if:
  - Two devices claim same node_id → collision
  - node_id reassigned → routing breaks
  - No collision handling → undefined behavior
```

---

## Target Design (Post-Test Roadmap)

```
Architecture:
  MAC = "aa:bb:cc:dd:ee:ff"  ← Primary (software layer, immutable)
  node_id = "node-30"  ← Unique alias (human layer, regenerated on collision)

Benefits:
  ✓ MAC is immutable, globally unique
  ✓ Survives firmware reboots, factory resets
  ✓ Software routing uses MAC (efficient, binary)
  ✓ Humans use node_id (friendly alias)
  ✓ Collisions resolved automatically
```

---

## Architecture Layers

### Layer 1: Device Firmware (MAC-Based Identity)

**Device Boots:**
```
1. Read MAC from ESP32 efuse (immutable hardware ID)
2. Generate unique node_id if not in NVS:
   - Format: "node-XX" where XX = MAC[4:6] in hex
   - Example: MAC aa:bb:cc:dd:ee:ff → "node-eeff"
3. Store node_id in NVS (persistent, but can be regenerated)
4. Publish status to MQTT:
   {
     "mac": "aa:bb:cc:dd:ee:ff",  ← PRIMARY
     "node_id": "node-eeff",  ← ALIAS
     "uptime_ms": 123456,
     "neighbors_mac": ["aa:bb:cc:11:22:33", "aa:bb:cc:44:55:66"]  ← MACs, not node_ids
   }
```

**ControlPacket Addressing:**
```
Current:
  struct {
    uint8_t target[6];  // "node-30" as string (inefficient)
  }

Better:
  struct {
    uint8_t target_mac[6];  // aa:bb:cc:dd:ee:ff (direct, binary)
    uint8_t target_node_id[8];  // optional alias for debugging
  }
```

### Layer 2: Daemon (Collision Detection & Resolution)

**Peer Registry (MAC-Primary):**
```python
peer_registry = {
    "aa:bb:cc:dd:ee:ff": MeshPeer {
        mac: "aa:bb:cc:dd:ee:ff",  # PRIMARY KEY (immutable)
        node_id: "node-eeff",  # ALIAS (human-readable, unique, regenerated on collision)
        alias_source: "firmware-generated",  # or "user-assigned", "collision-recovery"
        last_seen: timestamp,
        neighbors_mac: ["aa:bb:cc:11:22:33", ...],  # Store MACs, not node_ids
        rssi_dbm: -75,
        reachable: true
    }
}
```

**Collision Detection:**
```python
def handle_device_status(mac, status):
    node_id = status["node_id"]

    # Check if MAC already exists
    if mac in peer_registry:
        existing = peer_registry[mac]
        if existing.node_id == node_id:
            # No collision, update normally
            update_peer(mac, status)
        else:
            # Node_id changed (device rebooted, NVS corrupted)
            log_warning(f"MAC {mac} changed node_id: {existing.node_id} → {node_id}")
            update_peer(mac, status)
        return

    # Check if node_id already claimed by different device
    existing_device = find_peer_by_node_id(node_id)
    if existing_device and existing_device.mac != mac:
        # COLLISION! Two devices claim same node_id
        # Resolution: Regenerate conflicting node_id
        new_node_id = generate_unique_node_id(mac)

        log_error(f"Collision: {mac} claims {node_id}, but {existing_device.mac} owns it")
        log_info(f"Assigning {mac} → {new_node_id} instead")

        # Store resolution in daemon (optionally send back to device via MQTT)
        peer_registry[mac] = MeshPeer(
            mac=mac,
            node_id=new_node_id,
            alias_source="daemon-collision-recovery",
            ...
        )

        # Notify device of new node_id (optional command)
        notify_device_of_alias(mac, new_node_id)
    else:
        # No collision, register new device
        register_peer(mac, status)
```

**Routing (MAC-Based):**
```python
def route_command(cmd):
    # Command specifies target by node_id (for webapp UX)
    target_node_id = cmd.target_node

    # Daemon resolves node_id → MAC
    target_mac = resolve_node_id_to_mac(target_node_id)

    if not target_mac:
        return error("Unknown node_id")

    # Routing uses MAC (efficient, unambiguous)
    path = find_path_by_mac(target_mac)

    # ControlPacket uses MAC for addressing
    publish_command_with_mac(target_mac, cmd)
```

### Layer 3: Webapp (Human Layer)

**UX:**
```
User sees: "node-30" (human-friendly)
Daemon sends: MAC "aa:bb:cc:dd:ee:ff" in packets
Device understands: MAC addressing (software layer)

If collision occurs:
  Daemon auto-resolves, webapp shows: "node-30 (auto-recovered)"
```

---

## Collision Scenarios & Resolution

### Scenario 1: User Assigns Same node_id to Two Devices

```
Device A: MAC aa:bb:cc:11:11:11, node_id "relay-master" (user-assigned)
Device B: MAC aa:bb:cc:22:22:22, claims "relay-master" (user-assigned)

Resolution:
  1. Daemon detects collision (both MACs claim same node_id)
  2. First device keeps "relay-master" (earlier registration)
  3. Second device auto-assigned "node-2222" (derived from MAC)
  4. Daemon notifies user: "Device 2 auto-renamed to node-2222 (collision)"
  5. User can manually reassign if desired (device updates NVS)
```

### Scenario 2: Device NVS Corrupted (node_id Lost)

```
Device: MAC aa:bb:cc:dd:ee:ff, NVS cleared
Boot: Device generates new node_id "node-eeff" (from MAC suffix)
Daemon: Recognizes MAC, accepts new node_id (same MAC = same device)
Result: No collision, seamless recovery
```

### Scenario 3: Device Factory Reset

```
Device: MAC aa:bb:cc:dd:ee:ff
Before: node_id "relay-kitchen" (user-assigned)
After reset: node_id "node-eeff" (firmware-generated)

Daemon detects:
  - MAC unchanged (same device)
  - node_id changed (firmware regenerated)
  - Updates registration
  - Optionally notifies: "Device recovered from reset"

User can reassign node_id later if desired
```

---

## Implementation Phases

### Phase 50.1: Tonight (Current Design)
- ✅ node_id primary, MAC metadata
- ✅ Fleet test validation
- ⚠️ No collision handling yet

### Phase 50.2: Tomorrow (MAC-Primary Migration)

**Step 1: Firmware Update (AG)**
- Store MAC as primary in all communications
- Include MAC in MQTT status + ControlPacket
- Optionally allow user-assigned node_id in NVS
- On conflict, regenerate node_id from MAC

**Step 2: Daemon Update (Claude)**
- Change peer registry key: node_id → MAC
- Implement collision detection
- Auto-generate unique node_id on conflicts
- Maintain node_id → MAC resolution table

**Step 3: ControlPacket Update (both)**
- Use 6-byte MAC for device addressing
- Option: MAC-seeded encryption keys
- Benchmark addressing efficiency

**Step 4: Validation (AG)**
- Test deliberate collisions
- Verify NVS recovery
- Verify multi-device scenarios

### Phase 50.3: Production Hardening (Post-Phase 50)
- Persistent alias mappings in daemon (database)
- User interface for manual node_id assignment
- Audit log for collision events
- MAC-based device onboarding
- Backup/restore with MAC verification

---

## Data Flow: MAC-Primary Model

```
User Command (Webapp):
  POST /api/mesh/command
  {
    "target_node": "node-30",  ← Human-friendly
    "action": "gpio_toggle",
    "pin": 32
  }
          ↓
Daemon Resolution:
  node_id "node-30" → MAC "aa:bb:cc:dd:ee:ff"
          ↓
Command Publishing (MQTT):
  Topic: device/aa:bb:cc:dd:ee:ff/mesh/command
  Payload: {
    "cmd_id": "...",
    "action": "gpio_toggle",
    "pin": 32,
    "target_mac": "aa:bb:cc:dd:ee:ff"
  }
          ↓
Device Reception:
  MAC address = "aa:bb:cc:dd:ee:ff" (matches my MAC)
  Execute command
  Publish ACK with MAC: device/aa:bb:cc:dd:ee:ff/mesh/ack
          ↓
Daemon Tracking:
  MAC "aa:bb:cc:dd:ee:ff" ACK'd command "..."
  Resolve MAC → node_id "node-30" for webapp
  Return: "node-30 command completed"
          ↓
User Sees:
  "node-30: command completed" (human-friendly)
```

---

## Benefits Summary

| Aspect | Current | MAC-Primary |
|--------|---------|-------------|
| **Primary ID** | node_id (string) | MAC (bytes) |
| **Immutability** | Changeable | Fixed hardware |
| **Collision Risk** | High (user error) | Low (auto-resolved) |
| **Addressing Efficiency** | String lookup | Binary compare |
| **Recovery from Reset** | Manual | Automatic |
| **Scalability** | Limited | Unlimited |
| **Encryption Seeds** | Not available | MAC-seeded keys |
| **Human Friendliness** | Direct names | Alias system |

---

## Questions for Roadmap

1. **Should we implement Phase 50.2 before shipping, or validate mesh first?**
   - Current plan: Validate tonight (Phase 50.1), implement MAC-primary tomorrow (Phase 50.2)

2. **Should node_id be user-assignable or firmware-generated only?**
   - Proposal: Firmware-generated (immutable alias), user can override in MQTT or via CLI

3. **Should daemon persist collision resolutions?**
   - Proposal: In-memory for now, SQLite for production

4. **Should we include MAC-seeded encryption in Phase 50.2?**
   - Proposal: Yes (efficient, improves security)

---

## Success Criteria (Phase 50.2)

- [ ] MAC is primary identifier in all software layers
- [ ] node_id is unique per swarm, regenerated on collision
- [ ] Devices address each other by MAC
- [ ] MQTT topics reference MAC
- [ ] Daemon auto-resolves node_id conflicts
- [ ] User sees human-friendly aliases (node_id)
- [ ] System survives: reboots, resets, deliberate collisions
- [ ] ControlPacket uses 6-byte MAC addressing

---

**Owner:** AG (firmware) + Claude (daemon)
**Timeline:** Phase 50.2 = 2026-03-27 morning
**Dependency:** Phase 50 mesh validation (tonight)
