---
status: planning
owner: antigravity
---

# Spec: V2 Firmware Architectural Rationalization

## 1. Objective
Transform the procedural, singleton-heavy V2 firmware into a robust, industrial-grade event-driven system with clean abstractions and formal state management.

## 2. Key Abstractions

### A. The System Manager (Orchestrator)
-   **Class**: `SystemManager` (Singleton)
-   **Role**: Manage global state transitions (`BOOTING` -> `ONLINE`).
-   **Responsibility**: Execute the `BootStager` sequence. 
    -   *Phase 1 (Hardware)*: Power, I2C, LittleFS, NVS.
    -   *Phase 2 (Transport)*: Radio, WiFi, BLE, ESP-NOW.
    -   *Phase 3 (Service)*: MQTT, HTTP API, Product Manifests.
-   **Visuals**: Drive `OLEDManager` boot progress directly from the stager.

### B. The Command Registry (Decoupling)
-   **Method**: Replace `if-else` chain in `CommandManager` with a `CommandDispatcher` class.
-   **Registration**: Components (like `GPSManager` or `PluginX`) register their command strings (`"GPS"`, `"RELAY"`) and callbacks at runtime.
-   **Response**: Standardize on `JsonDocument` for all command returns.

### C. The Transport Adapter (Modularity)
-   **Interface**: `ITransportAdapter`
-   **Contract**: 
    -   `bool init()`
    -   `bool send(uint8_t dest, const uint8_t* data, size_t len)`
    -   `TransportStatus getStatus()`
-   **Goal**: Allow `MessageRouter` to loop through all registered adapters without knowing their internals.

## 3. Implementation Guardrails (Repository Safety)
-   **Branch**: All refactor work must occur on `feature/v2-rationalization`.
-   **No "Superficial" Edits**: Do not change logic unless it aligns with the new abstraction layer.
-   **Documentation**: Update `ARCHITECTURE_MAP.md` concurrently.

## 4. Handoff Strategy
-   **Antigravity (Phase 3)**: Design and Audit.
-   **Claude (Phase 2)**: Implementation of the `SystemManager` and `CommandRegistry` skeletons.
