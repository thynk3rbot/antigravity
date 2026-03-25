# LoRaLink V2 Rationalization & Autonomous Mesh-First Spec

## 1. Goal
Rationalize the LoRaLink V2 firmware from a monolithic `main.cpp` into a modular, variant-agnostic architecture while implementing "Autonomous Mesh-First" discovery and security.

## 2. Architectural Decisions

### A. Modular Decomposition (Phase 3 of Master Plan)
Extract system logic from `main.cpp` into the following components:
- **`BootSequence` ($SystemManager$)**: Staggered init of HAL, NVS/LittleFS, Power, Transports, and Apps.
- **`ControlLoop`**: 100Hz system loop for telemetry, OLED updates, power monitoring, and discovery beacons.
- **`MessageHandler`**: Centralized packet dispatcher (ACTION, TELEMETRY, ACK, HEARTBEAT, DISCOVERY).

### B. Autonomous Security (MAC-Seeded Derivation)
Enable peer-to-peer AES encryption without a PC-based registration handler.
- **Algorithm**: `shared_key = SHA256(sort(ourMAC, peerMAC) + network_secret)`
- **NVS Storage**: `network_secret` (16-byte blob) stored in NVS `loralink` namespace under key `net_secret`.
- **Identity**: Nodes identified by 6-byte MAC. `dev_name` key in NVS remains authoritative for local UI hostname.

### C. Discovery Protocol (LORA_BEACON)
Dedicated packet type for mesh auto-discovery.

- **Type**: `0xFD` (PacketType::DISCOVERY)
- **Payload (6 bytes)**:
  - `node_id`: uint16 (MAC suffix)
  - `hw_variant`: uint8 (2=V2, 3=V3, 4=V4)
  - `capabilities`: uint8 (Bitfield: BIT0=Relay, BIT1=Sensor, BIT2=GPS, BIT3=MQTT)
  - `hop_count`: uint8 (0=direct)
  - `rssi`: int8 (last measured RSSI)

## 3. Implementation Status & File Map

### Core (Implemented/Drafted)
- `lib/App/nvs_manager.h/.cpp`: Added `getDerivedKey()` and `setNetworkSecret()`.
- `lib/App/boot_sequence.h/.cpp`: Extracted setup logic.
- `lib/App/control_loop.h/.cpp`: Extracted system loop + added 5s discovery trigger.
- `lib/App/message_handler.h/.cpp`: Extracted packet dispatch logic.

### Infrastructure (To Implement/Modify)
- `lib/Transport/espnow_transport.h/.cpp`: Implement auto-peeering on `DISCOVERY` receipt.
- `lib/Transport/message_router.h/.cpp`: Update to support per-peer derived keys.
- `lib/App/command_registry.h/.cpp` (Phase 4): Replace `CommandManager` if-else chains.

## 4. Verification Plan

### Automated
- Build Matrix: `heltec_v2`, `heltec_v3`, `heltec_v4` must compile clean.
- Unit Test: `NVSManager::getDerivedKey` outputs same key on two nodes with swapped MACs.

### Manual
- Flash V3 and V4. Confirm they see each other on OLED "Neighbors" page without manual registration.
- Verify Relay Action still works using derived keys.

## 5. Deployment Constraints
- **VEXT Stagger**: Must stabilize before I2C calls (PROCESSES.md compliance).
- **ID Persistence**: `dev_name` must NOT be wiped during refactor.
