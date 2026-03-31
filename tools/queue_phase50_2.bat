@echo off
REM Queue all Phase 50.2 Ollama code generation tasks
REM Run this once fleet test confirms devices are registering successfully
REM
REM Expected output: 5 task IDs for tracking
REM Check progress: ollama_queue.bat check
REM Process queue: ollama_queue.bat process (if Ollama app running in background)

setlocal enabledelayedexpansion

set "MODEL=qwen2.5-coder:14b"
set "TIMEOUT=600"

echo.
echo ============================================
echo Phase 50.2 Ollama Task Batch Queue
echo Model: %MODEL%
echo ============================================
echo.

REM Task 1: MAC-Primary Peer Registry
echo [1/5] Queuing: MAC-Primary Peer Registry (mesh_router.py)
call ollama_queue.bat queue "%MODEL%" "Based on PHASE_50_2_DAEMON_SPEC.md, generate Python implementation for Phase 50.2 daemon MAC-primary architecture. FILE: daemon/src/mesh_router.py. GENERATE: (1) Updated MeshPeer dataclass with mac (PRIMARY KEY), node_id, alias_source, neighbors_mac fields (2) _generate_unique_node_id(mac) method returning 'node-XXYY' format (3) _detect_and_handle_collision(mac, status) method handling 4 collision scenarios: same MAC/node_id, firmware reboot, new MAC/duplicate node_id, new MAC/new node_id (4) Updated update_peer_status() using new registries and collision detection (5) Resolution methods: resolve_node_id_to_mac(), resolve_mac_to_node_id(), get_peer_by_mac(), get_peer_by_node_id() (6) Updated __init__ for MAC-primary registries. Use dataclass, comprehensive docstrings, preserve MeshCommand logic, add collision logging. OUTPUT: ~400 lines Python, ready to integrate."
set "TASK1_QUEUED=1"

REM Task 2: API Endpoints
echo [2/5] Queuing: API Endpoint Updates (mesh_api.py)
call ollama_queue.bat queue "%MODEL%" "Generate Phase 50.2 API endpoint updates for daemon/src/mesh_api.py. ENDPOINTS: (1) GET /api/mesh/topology - return peers with node_id (display), mac (new field), neighbors as MAC list, include resolution for debugging (2) POST /api/mesh/command - INPUT: target_node (node_id), action, params; INTERNALLY: resolve node_id to MAC; VALIDATE: check MAC in topology; PUBLISH: use MAC for routing; RETURN: both target_node and target_mac (3) GET /api/mesh/command/{cmd_id} - no changes. All endpoints backward compatible, add MAC resolution error handling (404 if unknown), use Pydantic models. OUTPUT: ~200 lines updated FastAPI routes."

REM Task 3: Collision Detection Tests
echo [3/5] Queuing: Collision Detection Unit Tests (test_phase_50_2_collisions.py)
call ollama_queue.bat queue "%MODEL%" "Generate comprehensive unit tests for Phase 50.2 daemon collision detection. FILE: daemon/tests/test_phase_50_2_collisions.py. TESTS: (1) test_no_collision_same_mac_same_node_id (2) test_firmware_reboot_same_mac_different_node_id (3) test_collision_different_mac_same_node_id (4) test_collision_deliberate_assignment (5) test_collision_factory_reset (6) test_resolution_tables_consistent (7) test_peer_lookup_by_mac_and_node_id. Use pytest, mock MeshTopology, realistic test data, each test <50 lines, all mappings verified for consistency. OUTPUT: ~300 lines, 7 test functions."

REM Task 4: E2E Integration Test
echo [4/5] Queuing: E2E Integration Test (test_phase_50_2_e2e.py)
call ollama_queue.bat queue "%MODEL%" "Generate Phase 50.2 end-to-end integration test. FILE: daemon/tests/test_phase_50_2_e2e.py. SIMULATE: Device A online with MAC+node_id, Device B online with same node_id (collision), daemon detects and auto-assigns unique node_id to B, user sends command to 'node-30', daemon resolves node_id to MAC, publishes to device/MAC/mesh/command, Device A receives and ACKs, daemon tracks completion. ASSERTIONS: both devices registered, B has recovered node_id, command routed to correct MAC, ACK logged, topology correct. Use pytest-asyncio, mock MQTT with callbacks, realistic timing, test success and failure paths. OUTPUT: ~250 lines."

REM Task 5: Firmware Updates (Optional - AG may handle)
echo [5/5] Queuing: Firmware Phase 50.2 Updates (firmware updates)
call ollama_queue.bat queue "%MODEL%" "Generate Phase 50.2 firmware updates for device side. BACKGROUND: ControlPacket currently uses string node_id, target is 6-byte MAC for direct addressing. GENERATE: (1) Updated ControlPacket struct with uint8_t target_mac[6] field (PRIMARY), keep optional node_id for compat, update makeTelemetry() and makeHeartbeat() to include MAC (2) MQTT Status Publisher - publish 'mac' field in device/{node_id}/status as 'aa:bb:cc:dd:ee:ff' in every status (3) Command Handler - receive from device/{MAC}/mesh/command, extract target_mac, route via ControlPacket with MAC addressing. Minimal changes, backward compatible with Phase 50.1, compile-time flag for Phase 50.2, include comments. OUTPUT: ~150 lines."

echo.
echo ============================================
echo All 5 tasks queued!
echo ============================================
echo.
echo Next steps:
echo   1. Let Ollama process the queue (runs in background)
echo   2. Check progress: ollama_queue.bat check
echo   3. Review generated code in results directory
echo   4. Integrate into Phase 50.2 implementation
echo.
echo Results location: %%APPDATA%%\Magic\ollama_results\
echo.
pause
