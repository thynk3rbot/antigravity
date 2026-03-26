# Phase 50.2 Ollama Task Prompts

**Status:** Ready to queue after fleet test validation (2026-03-26 evening)

Use these prompts with `ollama_queue.bat queue "qwen2.5-coder:14b" "PROMPT_TEXT"` to generate Phase 50.2 implementation code asynchronously.

---

## Task 1: MAC-Primary Peer Registry (mesh_router.py)

**Model:** qwen2.5-coder:14b
**Estimated Time:** 2-3 minutes

```
Based on this specification (https://github.com/user/repo/blob/main/docs/PHASE_50_2_DAEMON_SPEC.md), generate the Python implementation for Phase 50.2 daemon MAC-primary architecture.

SPECIFICATION OUTLINE:
- Current: peer_registry keyed by node_id (string)
- Target: peer_registry keyed by MAC address (primary), node_id as unique alias
- Add resolution tables: node_id_to_mac, mac_to_node_id
- Implement collision detection for duplicate node_ids
- Auto-generate unique node_ids from MAC suffix on conflict
- Include alias_source tracking (firmware-generated vs daemon-collision-recovery)

FILE: daemon/src/mesh_router.py

GENERATE:
1. Updated MeshPeer dataclass with: mac (PRIMARY KEY), node_id, alias_source, neighbors_mac
2. _generate_unique_node_id(mac) method - generate "node-XXYY" from MAC
3. _detect_and_handle_collision(mac, status) method with 4 collision scenarios
4. Updated update_peer_status() method using new registries and collision detection
5. New resolution methods: resolve_node_id_to_mac(), resolve_mac_to_node_id(), get_peer_by_mac(), get_peer_by_node_id()
6. Updated __init__ to initialize MAC-primary registries

CONSTRAINTS:
- Use dataclass for MeshPeer (Python 3.7+)
- Include comprehensive docstrings
- Preserve existing MeshCommand and command tracking logic unchanged
- Add logging for collisions and recovery actions
- Match existing code style (2-space indent, type hints)

OUTPUT: Complete Python file ready to integrate, ~400 lines
```

---

## Task 2: Phase 50.2 Daemon API Updates (mesh_api.py)

**Model:** qwen2.5-coder:14b
**Estimated Time:** 2 minutes

```
Generate Phase 50.2 API endpoint updates for daemon/src/mesh_api.py

BACKGROUND:
- Current: GET /api/mesh/topology returns peers keyed by node_id
- Target: Same API surface (backward compatible), but routing uses MAC internally

GENERATE THESE ENDPOINTS:

1. GET /api/mesh/topology
   - Return peers with node_id (human display), mac (new field), neighbors as MAC list
   - Include resolution in response for debugging
   - Example: {"peers": [{"node_id": "node-30", "mac": "aa:bb:cc:dd:ee:ff", ...}]}

2. POST /api/mesh/command
   - INPUT: target_node (node_id), action, params
   - INTERNALLY: resolve node_id → MAC using topology.resolve_node_id_to_mac()
   - VALIDATE: Check MAC exists in topology
   - PUBLISH: Use MAC for internal routing
   - RETURN: Include both target_node (echo input) and target_mac (for reference)

3. GET /api/mesh/command/{cmd_id}
   - No changes (internal use of MAC, same output)

CONSTRAINTS:
- All endpoints remain backward compatible (input/output formats)
- Add MAC resolution error handling (404 if node_id unknown)
- Use Pydantic models for validation
- Include comprehensive docstrings

OUTPUT: Updated FastAPI route functions, ~200 lines
```

---

## Task 3: Phase 50.2 Daemon Collision Detection Unit Tests

**Model:** qwen2.5-coder:14b
**Estimated Time:** 3 minutes

```
Generate comprehensive unit tests for Phase 50.2 daemon collision detection

FILE: daemon/tests/test_phase_50_2_collisions.py

GENERATE TESTS FOR THESE SCENARIOS:

1. test_no_collision_same_mac_same_node_id()
   - MAC unchanged, node_id unchanged after reboot
   - Expected: Normal update, no recovery

2. test_firmware_reboot_same_mac_different_node_id()
   - MAC same, node_id changed (device rebooted, NVS regenerated)
   - Expected: Update node_id, source='firmware'

3. test_collision_different_mac_same_node_id()
   - Two different MACs claim same node_id
   - Expected: Auto-recovery, second MAC gets new node_id

4. test_collision_deliberate_assignment()
   - User assigns same node_id to two devices
   - Expected: Collision detected, second device recovered

5. test_collision_factory_reset()
   - Device factory reset, regenerates node_id from MAC
   - Expected: No collision if MAC is unique

6. test_resolution_tables_consistent()
   - Add 10 peers, verify node_id_to_mac and mac_to_node_id are bidirectional
   - Expected: All mappings consistent

7. test_peer_lookup_by_mac_and_node_id()
   - Add peer, lookup by MAC and by node_id
   - Expected: Same peer returned

CONSTRAINTS:
- Use pytest framework
- Create mock MeshTopology instance for each test
- Include test data (realistic MACs, node_ids)
- Mock MQTT callbacks
- Each test <50 lines

OUTPUT: Complete test file, ~300 lines, 7 test functions
```

---

## Task 4: Phase 50.2 Integration Test (E2E)

**Model:** qwen2.5-coder:14b
**Estimated Time:** 3-4 minutes

```
Generate Phase 50.2 end-to-end integration test simulating full mesh command flow

FILE: daemon/tests/test_phase_50_2_e2e.py

SIMULATE THIS FLOW:
1. Device A comes online, publishes status with MAC + node_id
2. Device B comes online, same node_id (deliberate collision)
3. Daemon detects collision, auto-assigns unique node_id to B
4. User sends command via API to "node-30" (Device A)
5. Daemon resolves node_id → MAC
6. Daemon publishes command to device/MAC/mesh/command
7. Device A receives and ACKs
8. Daemon tracks command completion

TEST ASSERTIONS:
- Both devices registered after initialization
- Device B has recovered node_id (not same as A)
- Command routed to correct MAC based on node_id
- ACK received and logged
- Topology shows both devices with correct MACs

ASYNC STRUCTURE:
- Use pytest-asyncio for async test functions
- Mock MQTT client with callback simulation
- Test timing constraints (command publish < 100ms, ACK within 5s)

CONSTRAINTS:
- No external dependencies (mock everything)
- Realistic timing (use asyncio.sleep for delays)
- Include comments explaining flow
- Test both success and failure paths

OUTPUT: Complete test file, ~250 lines
```

---

## Task 5: Phase 50.2 Firmware Code (Device Side - Optional)

**Model:** qwen2.5-coder:14b
**Estimated Time:** 4-5 minutes
**Note:** AG may handle this, only queue if waiting for AG

```
Generate Phase 50.2 firmware updates for device/src/main.cpp and ControlPacket

BACKGROUND:
- Current: ControlPacket uses string node_id for addressing
- Target: Use 6-byte MAC for direct addressing in ControlPacket
- Device firmware publishes MAC in MQTT status

GENERATE:

1. Updated ControlPacket struct
   - Add 6-byte MAC field: uint8_t target_mac[6]
   - Keep backward compat with optional node_id field
   - Update makeTelemetry(), makeHeartbeat() to include MAC

2. MQTT Status Publisher
   - Publish "mac" field in device/{node_id}/status
   - Format: "aa:bb:cc:dd:ee:ff"
   - Include in every status message

3. Command Handler
   - Receive command from device/{MAC}/mesh/command
   - Extract target_mac from payload
   - Route via ControlPacket with target_mac addressing

CONSTRAINTS:
- Minimal changes to existing firmware
- Backward compatible with Phase 50.1
- Add compile-time flag to enable Phase 50.2 features
- Include comments on MAC addressing

OUTPUT: Updated firmware components, ~150 lines
```

---

## Queue Commands (Ready to Execute)

Once fleet test completes and is validated:

```bash
# Task 1: MAC-primary peer registry
ollama_queue.bat queue "qwen2.5-coder:14b" "[PROMPT FROM TASK 1 ABOVE]"

# Task 2: API endpoint updates
ollama_queue.bat queue "qwen2.5-coder:14b" "[PROMPT FROM TASK 2 ABOVE]"

# Task 3: Collision detection tests
ollama_queue.bat queue "qwen2.5-coder:14b" "[PROMPT FROM TASK 3 ABOVE]"

# Task 4: E2E integration test
ollama_queue.bat queue "qwen2.5-coder:14b" "[PROMPT FROM TASK 4 ABOVE]"

# Then check results:
ollama_queue.bat check

# Process queue (if Ollama not running):
ollama_queue.bat process
```

---

## Timeline

| Time | Action |
|------|--------|
| **Now (00:50)** | AG flashing, daemon starting, fleet test beginning |
| **+30min (01:20)** | Fleet test in progress, waiting for results |
| **+60min (02:20)** | Fleet test complete, results analyzed |
| **2:30-4:00** | Queue all 5 Ollama tasks, let them run async |
| **4:00-6:00** | Ollama generating code (~2-5 min per task) |
| **6:00+** | Code ready for review and integration |

---

**Ready to proceed with Phase 50.2 once fleet test confirms pure mesh model works correctly.** 🚀

