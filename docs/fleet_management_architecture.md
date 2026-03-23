# LoRaLink Fleet Management Architecture

This document outlines the dual-track strategy for high-efficiency provisioning and operational management of LoRaLink device swarms.

## 1. Primary Use Cases

### A. Factory Provisioning (Swarm Deployment)
*   **Goal**: Rapidly move devices from "unboxed" to "production-ready".
*   **Workflow**:
    1.  **Golden Image**: Flash all devices with a standard firmware that defaults to `state: unconfigured`.
    2.  **Discovery**: Webapp identifies all nodes with `state: unconfigured` via mDNS TXT or Serial probe.
    3.  **Bootstrap**: Bulk-push `SETWIFI`, `SETNODEID`, and initial `/schedule.json` payload.
    4.  **Verification**: Final status check ensuring MAC, IP, and hardware version are persisted.

### B. Production Management (In-Field Operation)
*   **Goal**: Command, control, and update the deployed fleet.
*   **Workflow**:
    1.  **Deduplicated View**: Use MAC-based identity to maintain a stable registry across WiFi/Mesh.
    2.  **Health Monitoring**: Periodic `STATUS` heartbeats track battery, RSSI, and uptime.
    3.  **Bulk Updates**: Fleet-wide OTA updates and schedule re-synchronization via the `/api/fleet/*` endpoints.

## 2. Autodiscovery Enhancements

To support these use cases, the mDNS TXT record schema is extended:

| Key | Value | Description |
| :--- | :--- | :--- |
| `id` | `String` | Human-friendly Node ID (e.g., "Peer1") |
| `mac` | `String` | Immutable HW identifier (e.g., "A1:B2...") |
| `state` | `provisioned` / `unconfigured` | Current setup status |
| `type` | `loralink-gateway` | Device classification |
| `hw` | `V2` / `V3` / `V4` | Hardware generation |

## 3. Implementation Roadmap

### Phase 1: Provisioning Logic (Server-side)
- [ ] **State Detection**: Update `NodeConfig` to track `provisioned` boolean.
- [ ] **Provisioning Wizard**: UI bridge for bulk-setting WiFi/NodeIDs on unconfigured devices.
- [ ] **Template Engine**: Map `settings_template.json` to individual device MACs.

### Phase 2: Firmware Telemetry (v2+)
- [ ] **NVS State Tracking**: Add a "Provisioned" flag to NVS stored during the first `SETWIFI` / `SAVE` cycle.
- [ ] **Dynamic mDNS**: Update TXT records in real-time when state changes.

### Phase 3: Bulk Command Infrastructure
- [ ] **Targeted Multicast**: Send commands to a list of MAC addresses via the server's transport multiplexer.
- [ ] **Fleet OTA**: Sequential flashing of the entire registry with progress tracking.
