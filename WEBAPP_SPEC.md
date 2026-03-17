# LoRaLink Web Application Specification

This document serves as the primary technical and functional specification for the LoRaLink ecosystem's web interfaces. It consolidates the architectural requirements for both the **Fleet Administrator** (Centralized PC App) and the **Local Device Webserver** (Singleton/Gateway), ensuring they operate in harmony while serving distinct user needs.

---

## 1. System Architecture & Hierarchy

The system operates on a "Master-Slave" logic, balancing a macro-view (fleet management) with a micro-view (real-time device control).

### 1.1 Master-Slave Hierarchy
- **Master Node (PC/WebApp or High-Power Node)**: The Source of Truth. Dictates the "Network State" to ensure all units are synchronized.
  - **Heartbeat**: Broadcasts a `$Sync$` frame every 5 minutes.
  - **Interrupt**: Broadcasts immediately upon transport mode changes or Roll-Call triggers.
- **Slave Node (Heltec Units)**: Operates in a "Passive-Active" loop. Executes local tasks via the Task Scheduler but yields immediately to Master commands.

### 1.2 Multi-Transport Mapping
The system operates on a "Star-Mesh" hybrid architecture.

| Connection Path | Primary Transport | Protocol / Data Format |
| :--- | :--- | :--- |
| **PC → Gateway** | WiFi / Serial | JSON over HTTP/WebSockets |
| **Gateway → Node** | LoRa | Binary `MessagePacket` (64-byte) |
| **Node → Node** | LoRa / ESP-NOW | Binary `MessagePacket` (Mesh/Relay) |
| **Mesh → Local AI**| Serial | `AI_QUERY:` piped via the `ASK` command |

---

## 2. Communication Protocols

### 2.1 Transport Mnemonics (The "Mnemonic Switch")
The system supports four distinct data "shapes." The Master sends a single character to pivot the Slaves' output logic.

- **J (JSON)**: Full object with keys. Primary mode for WebApp widgets.
- **C (CSV)**: Strict comma-separated values. Optimized for high-speed logging.
- **K (KV)**: Key-Value pairs (e.g., `rssi:-102`). Ideal for raw terminal debugging.
- **B (BIN)**: Packed hex bytes. Used for maximum range and lowest airtime.

### 2.2 Verification & Roll-Call (AVRC)
Mechanism to confirm swarm health and synchronization.
- **Slotted Aloha Timing**: Nodes determine response windows based on Unique ID.
  - `Window = (NodeID % 10) * 150ms`.
  - The WebApp opens a 5-second "Collection Gate."
- **Pulse Packet**: Minimal response size: `@[NodeID]|[Mode]|[BattV]|[RSSI]@`.

---

## 3. Persistent Data Architecture (NVS)

The system utilizes the ESP32 Preferences library with namespace partitioning.

### 3.1 NVS Namespaces
- **`loralink`**: System core & network settings (`crypto_key`, `espnow_en`, `op_mode`, `pin_enabled`).
- **`espnow`**: Peer tracking and MAC address storage.
- **`lora_hw`**: Physical hardware state persistence (`RLY1`, `LED`, `VEXT`).
- **`pin_names`**: UI Labeling (e.g., `5 -> "Main Pump"`).
- **`hw_registry`**: **READ-ONLY** hardware identity (Board ID, HW Rev, Capabilities).

### 3.2 Administrative Pin Configuration (APC)
- **`pin_enabled` Flag**: Resides in `loralink`. The Fleet Admin is the sole authority to toggle this flag.
- **Interest List**: On boot, the device builds a runtime list of pins where `pin_enabled == true`.
- **Sparse Reporting**: Outgoing JSON payloads only contain keys for pins in the Interest List, minimizing "JSON Noise."

---

## 4. Hardware Safety & Power (Heltec V3)

### 4.1 Battery Management (Pin 1)
- **Nominal (>3.7V)**: All features enabled.
- **Low Power (3.4V-3.6V)**: Flag warning to Fleet Admin; throttle WiFi TX power.
- **Critical (<3.4V)**: **Auto-Shutdown.** Force all relays (Pins 5, 46, 6, 7) to LOW. Disable WiFi/BLE.

### 4.2 Relay & Sensor Safety
- **Relay Defaults**: Must default to LOW on boot unless specifically overridden and enabled.
- **VExt Control (Pin 36)**: Power to external sensors (e.g., DHT on Pin 15) is only active during measurement cycles to conserve energy.

---

## 5. Functional App Modes

### 5.1 Fleet Administrator (PC Mode)
- **Authority**: Primary. Can "Lock" NVS keys.
- **Batch Operations**: Push firmware or config changes to "Groups" or "Tags."
- **NVS Management**: Performs **Atomic Writes** via a staging area to ensure integrity over high-latency links.
- **GIS Mapping**: Uses `lat`/`lon` from `RemoteNode` for fleet visualization.

### 5.2 Local Device Webserver (Gateway/Singleton)
- **Authority**: Secondary/Emergency.
- **Real-time Control**: Low-latency (sub-100ms) pin toggles and sensor feedback.
- **Gateway Routing**: Encapsulates Serial/WiFi traffic into 64-byte LoRa `MessagePackets`.
- **Manual Overrides**: Key-Value targeting for specific pins without rewriting master schedules.

---

## 6. Scheduler Discipline (Ralph Bacon's Tasker)

To prevent radio interference and CPU jitter during critical sync/comm events:
- **Priority Levels**:
  - **Level 0 (Immediate)**: LoRa Radio Interrupts.
  - **Level 1 (Critical)**: Slotted Pulse Response.
  - **Level 2 (Normal)**: Scheduled tasks (OLED, DHT, Battery).
- **The "Silence" Flag (`isSyncing`)**: While true, the scheduler cycles but individual non-critical task functions must exit immediately.

---

## 7. Handshake & Security

- **Authentication**: Admin must provide the AES-128 `crypto_key` to establish a session via WiFi.
- **Manifest Discovery**: Upon auth, the device pushes a **Hardware Manifest JSON**:
  - `board_id` and `fw_ver`.
  - `cap_mask` (Available physical pins).
  - `pin_enabled` list.
  - `pin_names`.

---

**Hardware References**:
- [WiFi LoRa 32 (V3) Pinout Map](media/Heltec_pinmap.png)
- [System Specification](SYSTEM.md)
