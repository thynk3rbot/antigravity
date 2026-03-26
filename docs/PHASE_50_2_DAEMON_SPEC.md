# Phase 50.2: Daemon MAC-Primary Implementation Spec

**Scope:** Convert peer registry from node_id-keyed to MAC-keyed with collision detection

**Owner:** Claude (daemon)

**Timeline:** 2026-03-27 morning (after fleet test validation)

**Success Criteria:**
- All peer lookups use MAC as primary key
- Collision detection catches duplicate node_ids
- Auto-recovery generates unique node_ids on conflict
- Resolution table enables node_id → MAC translation for API compatibility

---

## Current State (Phase 50.1)

**File:** `daemon/src/mesh_router.py` (MeshTopology class)

```python
# Current peer registry structure
peer_registry: Dict[str, MeshPeer]  # keyed by node_id

@dataclass
class MeshPeer:
    node_id: str              # Primary in API, but changeable
    mac_address: str          # Hardware ID, immutable
    last_seen: float
    rssi_dbm: int
    reachable: bool
    neighbors: List[str]      # Currently stores node_ids
    battery_mv: Optional[int]
    uptime_ms: Optional[int]
```

**Issue:** Two devices can claim same `node_id`, breaking topology. No collision handling.

---

## Target State (Phase 50.2)

```python
# MAC-keyed registry
peer_registry: Dict[str, MeshPeer]  # keyed by MAC address (primary)

# Resolution table for API layer
node_id_to_mac: Dict[str, str]      # "node-30" → "aa:bb:cc:dd:ee:ff"
mac_to_node_id: Dict[str, str]      # "aa:bb:cc:dd:ee:ff" → "node-30"

@dataclass
class MeshPeer:
    mac: str                  # PRIMARY KEY (immutable, hardware)
    node_id: str              # ALIAS (unique, regenerable)
    alias_source: str         # "firmware-generated" | "user-assigned" | "daemon-collision-recovery"
    last_seen: float
    rssi_dbm: int
    reachable: bool
    neighbors_mac: List[str]  # Store MACs, not node_ids
    battery_mv: Optional[int]
    uptime_ms: Optional[int]
```

**Benefit:** MAC is immutable, collision-detectable, efficient for binary routing.

---

## Implementation Steps

### Step 1: Update MeshPeer dataclass

**File:** `daemon/src/mesh_router.py`

```python
@dataclass
class MeshPeer:
    mac: str                           # PRIMARY KEY
    node_id: str                       # Unique alias
    alias_source: str                  # Origin of alias
    last_seen: float
    rssi_dbm: int
    reachable: bool
    neighbors_mac: List[str]           # MACs instead of node_ids
    battery_mv: Optional[int] = None
    uptime_ms: Optional[int] = None

    def is_stale(self, threshold_ms: int = 120000) -> bool:
        """Check if peer hasn't been seen in threshold_ms."""
        import time
        current_ms = time.time() * 1000
        return (current_ms - self.last_seen) > threshold_ms
```

**Change list:**
- Rename `mac_address` → `mac` (shorter, primary)
- Add `alias_source` field (track origin of node_id)
- Rename `neighbors` → `neighbors_mac` (semantic clarity)
- Ensure `is_stale()` method exists unchanged

### Step 2: Add collision detection method

**File:** `daemon/src/mesh_router.py` — add to `MeshTopology` class

```python
def _generate_unique_node_id(self, mac: str) -> str:
    """Generate node_id from MAC suffix (fallback if collision detected)."""
    # Format: "node-XXYY" where XX:YY are last 2 MAC octets
    mac_suffix = mac.split(':')[-2:]  # ["ee", "ff"] from "aa:bb:cc:dd:ee:ff"
    return f"node-{''.join(mac_suffix).upper()}"

def _detect_and_handle_collision(
    self,
    mac: str,
    status: dict
) -> Tuple[str, str]:
    """
    Detect node_id collision. Return (final_node_id, collision_occurred).

    Collision scenarios:
    1. MAC already in registry with same node_id → no action
    2. MAC already in registry with different node_id → log, update
    3. New MAC claims existing node_id → collision! Auto-recover
    4. New MAC, new node_id → register normally
    """
    incoming_node_id = status.get("node_id", self._generate_unique_node_id(mac))

    # Case 1: MAC already registered
    if mac in self.peer_registry:
        existing = self.peer_registry[mac]
        if existing.node_id == incoming_node_id:
            # No collision, normal update
            return incoming_node_id, False
        else:
            # Node changed node_id (likely rebooted, NVS corrupted)
            logger.warning(
                f"[Collision] MAC {mac} changed node_id: "
                f"{existing.node_id} → {incoming_node_id}"
            )
            return incoming_node_id, False

    # Case 2: node_id already claimed by different device
    existing_device = self.node_id_to_mac.get(incoming_node_id)
    if existing_device and existing_device != mac:
        # COLLISION! Two devices claim same node_id
        recovered_node_id = self._generate_unique_node_id(mac)
        logger.error(
            f"[Collision] MAC {mac} claims node_id '{incoming_node_id}', "
            f"but {existing_device} already owns it. "
            f"Auto-assigning '{recovered_node_id}' instead."
        )
        return recovered_node_id, True

    # Case 3: New device, new node_id
    return incoming_node_id, False
```

### Step 3: Update `update_peer_status()` method

**File:** `daemon/src/mesh_router.py` — modify existing method

```python
def update_peer_status(self, mac: str, status: dict) -> None:
    """
    Update peer from device status message.

    MQTT message format:
    {
        "mac": "aa:bb:cc:dd:ee:ff",        ← PRIMARY
        "node_id": "node-30",               ← ALIAS (may be firmware-generated or user-assigned)
        "uptime_ms": 123456,
        "rssi_dbm": -75,
        "neighbors_mac": ["11:22:33:44:55:66", ...],  ← Store MACs
        ...
    }
    """
    import time

    # Detect and handle collisions
    final_node_id, collision_occurred = self._detect_and_handle_collision(mac, status)

    # Create or update peer
    peer = MeshPeer(
        mac=mac,
        node_id=final_node_id,
        alias_source="daemon-collision-recovery" if collision_occurred else "firmware",
        last_seen=time.time() * 1000,
        rssi_dbm=status.get("rssi_dbm", -999),
        reachable=True,
        neighbors_mac=status.get("neighbors_mac", []),
        battery_mv=status.get("battery_mv"),
        uptime_ms=status.get("uptime_ms"),
    )

    # Update registries
    self.peer_registry[mac] = peer

    # Update resolution tables
    # Remove old mapping if node_id changed
    old_mac = self.node_id_to_mac.get(final_node_id)
    if old_mac and old_mac != mac:
        del self.mac_to_node_id[old_mac]

    self.node_id_to_mac[final_node_id] = mac
    self.mac_to_node_id[mac] = final_node_id

    logger.info(
        f"[Peer] {final_node_id} (MAC {mac}): "
        f"online, RSSI={status.get('rssi_dbm', 'N/A')}dBm, "
        f"neighbors={len(peer.neighbors_mac)}"
    )
```

### Step 4: Add resolution methods for API compatibility

**File:** `daemon/src/mesh_router.py` — add to `MeshTopology` class

```python
def resolve_node_id_to_mac(self, node_id: str) -> Optional[str]:
    """Convert user-friendly node_id to hardware MAC for routing."""
    return self.node_id_to_mac.get(node_id)

def resolve_mac_to_node_id(self, mac: str) -> Optional[str]:
    """Convert hardware MAC to friendly node_id for display."""
    return self.mac_to_node_id.get(mac)

def get_peer_by_mac(self, mac: str) -> Optional[MeshPeer]:
    """Get peer by hardware MAC."""
    return self.peer_registry.get(mac)

def get_peer_by_node_id(self, node_id: str) -> Optional[MeshPeer]:
    """Get peer by friendly node_id (resolves MAC, then looks up)."""
    mac = self.resolve_node_id_to_mac(node_id)
    if mac:
        return self.peer_registry.get(mac)
    return None

def list_peers(self) -> List[MeshPeer]:
    """Get all peers from MAC-keyed registry."""
    return list(self.peer_registry.values())
```

### Step 5: Update `__init__` method

**File:** `daemon/src/mesh_router.py` — modify initialization

```python
def __init__(self, own_node_id: str = "daemon-0"):
    """Initialize topology tracker with MAC-primary architecture."""
    self.own_node_id = own_node_id
    self.own_mac = "FF:FF:FF:FF:FF:FF"  # Placeholder for daemon

    # MAC-primary registries
    self.peer_registry: Dict[str, MeshPeer] = {}       # MAC → MeshPeer
    self.node_id_to_mac: Dict[str, str] = {}           # node_id → MAC
    self.mac_to_node_id: Dict[str, str] = {}           # MAC → node_id

    # Command tracking (unchanged)
    self.commands: Dict[str, MeshCommand] = {}
    self.command_history: Deque[MeshCommand] = deque(maxlen=1000)
```

### Step 6: Update API endpoints for backward compatibility

**File:** `daemon/src/mesh_api.py` — modify existing endpoints

**Endpoint: GET /api/mesh/topology** (minimal change for backward compatibility)

```python
@router.get("/api/mesh/topology")
async def get_topology(topology: MeshTopology = Depends(get_topology_instance)):
    """Get mesh topology. Peer list uses node_id for human readability."""
    peers = []
    for peer in topology.list_peers():
        peers.append({
            "node_id": peer.node_id,      # Human-friendly name
            "mac": peer.mac,              # Hardware ID (new field)
            "reachable": peer.reachable,
            "rssi_dbm": peer.rssi_dbm,
            "neighbors": peer.neighbors_mac,  # Updated to store MACs
            "battery_mv": peer.battery_mv,
            "uptime_ms": peer.uptime_ms,
        })
    return {
        "node_count": len(peers),
        "online_count": sum(1 for p in peers if p["reachable"]),
        "peers": peers,
    }
```

**Endpoint: POST /api/mesh/command** (routing uses MAC internally)

```python
@router.post("/api/mesh/command")
async def send_command(
    req: SendCommandRequest,
    topology: MeshTopology = Depends(get_topology_instance),
    mqtt: MQTTClientManager = Depends(get_mqtt_instance),
):
    """Send command to device. Input uses node_id, routing uses MAC."""

    # Resolve node_id → MAC (for backward compatibility with webapp)
    target_mac = topology.resolve_node_id_to_mac(req.target_node)
    if not target_mac:
        raise HTTPException(status_code=404, detail=f"Unknown node_id: {req.target_node}")

    # Verify MAC exists in topology
    target_peer = topology.get_peer_by_mac(target_mac)
    if not target_peer:
        raise HTTPException(status_code=404, detail=f"Node {req.target_node} not in topology")

    # Create and publish command (use MAC for addressing)
    cmd_id = topology.create_command(
        target_mac=target_mac,
        action=req.action,
        params=req.params or {},
    )

    # Publish to MQTT with MAC addressing
    await mqtt.publish_command(target_mac, {
        "cmd_id": cmd_id,
        "action": req.action,
        "target_mac": target_mac,  # NEW: direct MAC for device routing
        **req.params or {}
    })

    return {
        "cmd_id": cmd_id,
        "status": "published",
        "target_node": req.target_node,      # Echo input for UX
        "target_mac": target_mac,            # Also return MAC
    }
```

### Step 7: Update MQTT handler for MAC messages

**File:** `daemon/src/mqtt_client.py` — modify status handler

```python
async def _on_device_status(self, msg):
    """
    Handle device status update from MQTT.
    Expected format:
    {
        "mac": "aa:bb:cc:dd:ee:ff",     ← PRIMARY
        "node_id": "node-30",           ← ALIAS
        "uptime_ms": 123456,
        "rssi_dbm": -75,
        "neighbors_mac": ["11:22:33:44:55:66", ...],
        ...
    }
    """
    try:
        payload = json.loads(msg.payload.decode())

        # Extract MAC (primary identifier)
        mac = payload.get("mac")
        if not mac:
            logger.warning(f"[MQTT] Status message missing 'mac' field: {payload}")
            return

        # Pass to collision detector (via update_peer_status)
        if self.on_device_status:
            await self.on_device_status(mac, payload)
    except Exception as e:
        logger.error(f"[MQTT] Error processing status: {e}")
```

---

## Data Flow: MAC-Primary Model (Phase 50.2)

```
Device Boot:
  1. Read MAC from efuse: "aa:bb:cc:dd:ee:ff"
  2. Generate node_id from MAC suffix: "node-eeff"
  3. Publish MQTT status:
     {
       "mac": "aa:bb:cc:dd:ee:ff",      ← PRIMARY
       "node_id": "node-eeff",          ← ALIAS
       ...
     }
        ↓
Daemon Receives:
  1. Collision detector runs
  2. If no collision: register MAC → MeshPeer, update resolution tables
  3. If collision: generate recovery node_id, store with source="daemon-collision-recovery"
        ↓
User sends command via Webapp:
  POST /api/mesh/command
  {
    "target_node": "node-eeff",  ← Human-friendly
    "action": "gpio_toggle",
    "pin": 32
  }
        ↓
Daemon routes:
  1. Resolve: "node-eeff" → "aa:bb:cc:dd:ee:ff"
  2. Verify peer exists by MAC
  3. Publish to MQTT:
     Topic: device/aa:bb:cc:dd:ee:ff/mesh/command
     Payload: {
       "cmd_id": "...",
       "action": "gpio_toggle",
       "pin": 32,
       "target_mac": "aa:bb:cc:dd:ee:ff"
     }
        ↓
Device receives and routes (via ControlPacket with MAC addressing)
        ↓
Device ACKs:
  Topic: device/aa:bb:cc:dd:ee:ff/mesh/ack
  Payload: {"cmd_id": "...", "success": true, ...}
        ↓
Daemon tracks:
  1. Match ACK to command by cmd_id
  2. Resolve MAC → node_id for webapp response
  3. Return: "node-eeff command completed"
```

---

## Testing Strategy

### Unit Tests

**test_collision_detection.py**:
```python
def test_no_collision_same_mac_same_node_id():
    """MAC unchanged, node_id unchanged → normal update."""

def test_firmware_rebooted_same_mac_new_node_id():
    """MAC unchanged, node_id changed → update with source='firmware'."""

def test_collision_different_mac_same_node_id():
    """Two MACs claim same node_id → auto-recovery."""

def test_collision_user_assigned():
    """User assigns same node_id to two devices → conflict."""

def test_collision_factory_reset():
    """Device reset clears NVS, regenerates node_id → no collision."""
```

### Integration Tests

**test_phase_50_2_e2e.py**:
```python
async def test_mac_based_command_routing():
    """Command sent via node_id, routed via MAC."""

async def test_topology_shows_correct_neighbors():
    """Neighbors stored as MACs, resolved for display."""

async def test_api_backward_compatibility():
    """Webapp still uses node_id in requests, daemon handles resolution."""
```

---

## Backward Compatibility Notes

✅ **API Input**: Webapp sends `target_node: "node-30"` — unchanged
✅ **API Response**: Still includes `node_id` for UX — unchanged
✅ **Internal**: All peer lookups now use MAC — transparent to caller
✅ **MQTT Topics**: Now include MAC — devices must support (firmware Phase 50.2)
⚠️ **MQTT Payload**: Now includes `"mac"` field — firmware must populate this

---

## Success Criteria

- [ ] `MeshTopology` initialized with MAC-primary registries
- [ ] Collision detection catches duplicate node_ids automatically
- [ ] Auto-recovery generates unique node_ids on conflict
- [ ] Resolution tables (`node_id_to_mac`, `mac_to_node_id`) maintain consistency
- [ ] API endpoints maintain backward compatibility (input/output unchanged)
- [ ] Daemon tracks command routing by MAC, resolves to node_id for display
- [ ] Unit tests pass for all collision scenarios
- [ ] Integration test passes for end-to-end MAC-based routing
- [ ] Fleet test with 3+ devices, deliberate collisions handled gracefully

---

## Files to Modify

1. `daemon/src/mesh_router.py` — Core logic (Steps 1-5)
2. `daemon/src/mesh_api.py` — Endpoints (Step 6)
3. `daemon/src/mqtt_client.py` — Event handlers (Step 7)
4. `daemon/tests/test_collision_detection.py` — New unit tests
5. `daemon/tests/test_phase_50_2_e2e.py` — New integration tests

---

**Ready to implement after fleet test validation.**

