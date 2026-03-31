@echo off
REM Phase 50.2 Master Ollama Task — Full Context Generation
REM
REM This queues a SINGLE comprehensive task to Ollama that includes:
REM - Full project context (Magic mesh architecture)
REM - Complete Phase 50.2 specification (MAC-primary design)
REM - All implementation requirements (daemon, firmware, tests)
REM - Code integration guidance
REM
REM Ollama processes this as ONE coordinated task with full understanding
REM Output: Complete Phase 50.2 implementation package (~1500-2000 lines)

setlocal enabledelayedexpansion

set "MODEL=qwen2.5-coder:14b"

echo.
echo ============================================
echo Phase 50.2 MASTER PROMPT
echo Queuing to Ollama for comprehensive generation
echo ============================================
echo.

call ollama_queue.bat queue "%MODEL%" ^
"You are a senior firmware and backend engineer implementing Phase 50.2 (MAC-Primary Architecture) for the Magic mesh networking system. ^
^
PROJECT CONTEXT: ^
- Platform: ESP32-S3 LoRa mesh (V2/V3/V4 boards) ^
- Current State: Phase 50.1 fleet test validates pure mesh model (devices route to each other via ControlPacket) ^
- Daemon: FastAPI Python server monitoring mesh topology via MQTT ^
- Devices: Publish status to device/{node_id}/status, receive commands from device/{node_id}/mesh/command ^
- Goal: Shift from node_id-primary to MAC-primary identification for robustness and efficiency ^
^
PHASE 50.2 PROBLEM: ^
- Current: Two devices can claim same node_id, breaking topology ^
- Root Cause: node_id is user-changeable, MAC is hardware-immutable ^
- Solution: Use MAC as primary key, node_id as unique regenerable alias with collision recovery ^
^
ARCHITECTURE REQUIREMENTS: ^
1. FIRMWARE DEVICE SIDE: ^
   - Publish MAC in MQTT status: device/{node_id}/status includes 'mac': 'aa:bb:cc:dd:ee:ff' ^
   - ControlPacket addressed by MAC (6-byte field) for direct routing ^
   - Generate node_id from MAC suffix if NVS corrupted (e.g., 'node-eeff' from MAC ending ee:ff) ^
^
2. DAEMON BACKEND (PYTHON): ^
   - Peer registry keyed by MAC (primary), not node_id ^
   - Resolution tables: node_id_to_mac and mac_to_node_id ^
   - Collision detection: if two MACs claim same node_id, auto-assign recovery node_id to second ^
   - API backward compatible: input/output still uses node_id, internal routing uses MAC ^
^
3. REST API ENDPOINTS: ^
   - GET /api/mesh/topology: return peers with node_id (display), mac (new), neighbors (MAC list) ^
   - POST /api/mesh/command: input target_node (node_id), internally resolve to MAC, publish with MAC ^
   - All endpoints maintain backward compatibility ^
^
4. TESTS: ^
   - Unit tests: collision scenarios (same MAC diff node_id, diff MAC same node_id, etc.) ^
   - E2E test: device registration → collision → recovery → command routing ^
   - All tests use pytest, mock MQTT, verify consistency ^
^
IMPLEMENTATION DELIVERABLES: ^
^
FILE 1: daemon/src/mesh_router.py (replace MeshPeer + registries) ^
  - MeshPeer dataclass: mac (PRIMARY KEY), node_id, alias_source, neighbors_mac, battery_mv, uptime_ms, rssi_dbm, reachable, last_seen ^
  - Static registries: peer_registry[mac] = MeshPeer, node_id_to_mac[node_id] = mac, mac_to_node_id[mac] = node_id ^
  - _generate_unique_node_id(mac): return 'node-' + last_2_mac_octets_hex (e.g., 'node-EEFF') ^
  - _detect_and_handle_collision(mac, status): 4 scenarios - (1) MAC exists, same node_id: no action (2) MAC exists, diff node_id: update (3) NEW MAC, existing node_id: COLLISION! auto-recover (4) NEW MAC, NEW node_id: register normally ^
  - update_peer_status(mac, status): call collision detector, update registries, maintain consistency ^
  - Resolution methods: resolve_node_id_to_mac(node_id), resolve_mac_to_node_id(mac), get_peer_by_mac(mac), get_peer_by_node_id(node_id) ^
  - Line count: ~400, include docstrings, logging for collisions ^
^
FILE 2: daemon/src/mesh_api.py (update endpoints) ^
  - GET /api/mesh/topology: list peers with {node_id, mac, rssi_dbm, neighbors_mac, reachable, battery_mv, uptime_ms} ^
  - POST /api/mesh/command: resolve target_node → MAC, validate, publish to MQTT device/{mac}/mesh/command, return {cmd_id, status, target_node, target_mac} ^
  - GET /api/mesh/command/{cmd_id}: no changes (internal use) ^
  - Error handling: 404 if node_id unknown, 400 for invalid params ^
  - Line count: ~200, Pydantic models, comprehensive docstrings ^
^
FILE 3: daemon/tests/test_phase_50_2_collisions.py ^
  - 7 test functions covering all collision scenarios ^
  - test_no_collision_same_mac_same_node_id: normal update ^
  - test_firmware_reboot_same_mac_different_node_id: device rebooted, NVS regenerated ^
  - test_collision_different_mac_same_node_id: two MACs claim same node_id, second recovered ^
  - test_collision_deliberate_assignment: user assigns same node_id, conflict detected ^
  - test_collision_factory_reset: device reset, regenerates from MAC ^
  - test_resolution_tables_consistent: 10 peers, all mappings bidirectional ^
  - test_peer_lookup_by_mac_and_node_id: same peer returned from both lookups ^
  - Use pytest, mock MeshTopology, each test <50 lines ^
  - Line count: ~300 ^
^
FILE 4: daemon/tests/test_phase_50_2_e2e.py ^
  - Async integration test using pytest-asyncio ^
  - Simulate: Device A registers → Device B registers with same node_id → collision detected → B auto-assigned unique node_id → user sends command to 'node-30' → daemon resolves to MAC → publishes to MQTT → device ACKs → command completed ^
  - Verify: topology correct, both devices present, MAC addressing used, ACK logged, no collisions ^
  - Mock MQTT with callbacks, realistic timing, test success and error paths ^
  - Line count: ~250 ^
^
FILE 5: firmware/v2/lib/App/control_packet.h (optional - AG may handle) ^
  - Add uint8_t target_mac[6] to ControlPacket header ^
  - Update makeTelemetry, makeHeartbeat to include source MAC ^
  - Keep node_id for backward compat ^
  - Line count: ~50 changes ^
^
FILE 6: firmware/v2/src/mqtt_transport.cpp (optional - AG may handle) ^
  - Publish 'mac' field in device/{node_id}/status ^
  - Format: 'aa:bb:cc:dd:ee:ff' from efuse MAC ^
  - Update every status message ^
  - Line count: ~20 changes ^
^
QUALITY REQUIREMENTS: ^
- All code follows project style (2-space indent for Python, match existing patterns) ^
- Type hints on all functions (Python) ^
- Comprehensive docstrings explaining collision handling ^
- Logging for debugging (especially collision scenarios) ^
- All imports correct, no external dependencies beyond existing (paho-mqtt, fastapi, pytest) ^
- Tests use mock MQTT, no real broker needed ^
- API responses include examples in docstrings ^
^
CONTEXT DOCUMENTS AVAILABLE: ^
- See: docs/PHASE_50_2_DAEMON_SPEC.md (detailed 7-step implementation plan) ^
- See: docs/PHASE_50_2_MAC_ARCHITECTURE.md (architecture overview, data flows) ^
^
OUTPUT FORMAT: ^
Generate COMPLETE PRODUCTION-READY code. Not pseudocode, not outlines — actual code ready to drop into the project. ^
Include clear file paths and line counts. ^
Assume Python 3.7+ with dataclasses. ^
Assume pytest for testing. ^
Assume asyncio for async code. ^
^
DELIVERABLE: ~1500-2000 lines total across 6 files. All integrated and ready for testing. ^
Make it clear and reviewable so Claude and AG can assess and integrate quickly."

echo.
echo ============================================
echo MASTER TASK QUEUED
echo ============================================
echo.
echo Ollama will generate COMPLETE Phase 50.2 implementation
echo with full understanding of architecture and requirements.
echo.
echo Expected output files:
echo   1. mesh_router.py (~400 lines) - MAC-primary registry
echo   2. mesh_api.py (~200 lines) - API endpoints
echo   3. test_phase_50_2_collisions.py (~300 lines) - Unit tests
echo   4. test_phase_50_2_e2e.py (~250 lines) - Integration test
echo   5. control_packet.h (~50 changes) - Firmware MAC field
echo   6. mqtt_transport.cpp (~20 changes) - Firmware status publish
echo.
echo Total: ~1500-2000 production-ready lines
echo.
echo Check results: ollama_queue.bat check
echo.
pause
