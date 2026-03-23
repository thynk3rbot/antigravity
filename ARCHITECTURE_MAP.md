# ARCHITECTURE_MAP.md

## Purpose

This file gives an agentic IDE a fast, stable mental model of the LoRaLink firmware repository so it does not have to rediscover the entire architecture from scratch every time.

It is a working architecture index, not a substitute for reading source files.

---

## 1. Project Identity

**LoRaLink-AnyToAny** is unified firmware focused on prioritized **any-to-any command routing** across multiple communications interfaces.

### Current Supported Board
- **Heltec WiFi LoRa 32 V2 / V3 / V4**
- ESP32-S3 (V3/V4) / ESP32 (V2)
- SX1262 (V3/V4) / SX1276 (V2) 915 MHz
- OLED over I2C
- VEXT power control
- **MAC-based Fleet Identity**: Unique hardware identification.

### Build / Flash
- PlatformIO
- `pio run`
- `pio run -t upload`

---

## 2. Core Architectural Style

The firmware follows a **Library-centric, specialized manager architecture**.

### Path Structure (v2)
- `firmware/v2/lib/HAL/`: Hardware Abstraction Layer (Board-specific configs).
- `firmware/v2/lib/Transport/`: Network and radio drivers (WiFi, LoRa, BLE, ESP-NOW).
- `firmware/v2/lib/App/`: Business logic (CommandManager, StatusBuilder, NVS).
- `firmware/v2/src/main.cpp`: System integration and task orchestration.

### Key Properties
- Components are decoupled into libraries for easier testing and board switching.
- Managers use `getInstance()` or static initialization.
- Cross-manager coordination is common
- **Identity-First Registry**: All nodes are keyed by their WiFi MAC address.
- Shared operational truth is centralized through manager interactions
- Boot order matters
- Runtime work is coordinated with scheduling rather than a heavy main loop

### Design Implication
This is **not** a microservice-style decomposition or a strongly dependency-injected architecture. It is an embedded control platform built around:
- singleton managers
- runtime scheduling
- transport routing
- persistence
- board-aware hardware control

---

## 3. Boot Model

The system uses **staggered boot** to reduce power spikes and brownout risk during radio / WiFi / OLED bring-up.

### Boot Intent
- sequence subsystem initialization carefully
- avoid simultaneous current spikes
- restore persistent state before activating dependent services
- preserve system stability on constrained hardware

### Things to Verify in Code
When reading `main.cpp`, identify:
- initialization order
- delay staging
- factory reset / button logic
- VEXT and display power sequencing
- when persistence is restored
- when each transport manager begins
- when scheduling begins

---

## 4. Core Managers

## `CommandManager`
**Role:** Universal command routing and interface-to-interface dispatch.

### Responsibilities
- central command registration
- parse incoming commands
- route messages from any `CommInterface`
- determine local vs forwarded handling
- unify command surface across transports

### Why It Matters
This is the architectural center of the firmware.

### Watch For
- oversized command surface
- duplicate registrations
- transport-specific behavior leaking into generic routing
- safety issues around hardware-affecting commands

---

## `LoRaManager`
**Role:** LoRa transport and radio control.

### Responsibilities
- radio initialization
- packet send/receive
- encryption behavior if present
- any mesh / forwarding / repeater logic
- radio-specific diagnostics and queue behavior

### Critical Dependencies
- SPI
- LoRa pins
- board pin mapping
- brownout-sensitive boot sequencing

---

## `ScheduleManager`
**Role:** Runtime scheduling and task orchestration.

### Responsibilities
- TaskScheduler integration
- periodic task execution
- dynamic task support
- scheduling limits and priorities

### Why It Matters
A large amount of system behavior is likely driven indirectly through scheduling rather than direct loops.

### Watch For
- task count limits
- dynamic task types
- task priority interactions
- coupling with commands and UI

---

## `WiFiManager`
**Role:** WiFi networking, compact web dashboard, config API, OTA.

### Responsibilities
- WiFi lifecycle
- embedded web UI
- config API
- OTA behavior
- route/response logic

### Why It Matters
This is both a transport manager and a user/admin surface.

### Watch For
- API changes that affect `tools/webapp/`
- HTML coupling with PC-side dashboard
- response schema drift
- admin logic mixed with transport logic

---

## `ESPNowManager`
**Role:** ESP-NOW peer and packet management.

### Responsibilities
- peer registry
- RX queue
- send/broadcast behavior
- persistence of peer data via NVS

### Known Constraints
- WiFi STA mode dependency
- `ESPNOW_MAX_PEERS=10`
- queue size = 8

### Watch For
- queue overflow
- peer struct drift
- changes requiring### Last Updated: 2026-03-23

## `MQTTManager`
**Role:** Telemetry and external command bridge.

### Responsibilities
- publish telemetry
- receive external commands
- integrate with WiFi lifecycle
- connect firmware to broker-facing workflows

### Watch For
- command topic consistency
- external command handling
- overlap with CommandManager semantics
- development diagnostics vs production telemetry

---

## `DataManager`
**Role:** Persistence and node tracking.

### Responsibilities
- NVS-backed persistence
- node and/or device registry state
- shared stored settings
- filesystem / Preferences interactions

### Why It Matters
This is a system memory anchor.

### Watch For
- config sprawl
- board-specific data leaking into generic storage
- mismatches between persisted config and runtime assumptions

---

## `MCPManager`
**Role:** MCP23017 GPIO expander control.

### Responsibilities
- I2C interaction with MCP23017
- interrupt-driven state handling
- extended GPIO support

### Critical Dependencies
- `Wire`
- shared I2C bus with OLED
- `PIN_MCP_INT=38`

### Watch For
- I2C contention
- expander pin naming integration
- required changes in `CommandManager` and `ScheduleManager`

---

## `ProductManager`
**Role:** Product deployment abstraction.

### Responsibilities
- deploy pin configurations
- deploy schedules
- deploy alerts atomically
- load from LittleFS `/products/`
- persist active product in NVS

### Why It Matters
This is a high-level deployment abstraction for applying coherent device behavior.

---

## 5. Communications Model

The project is built around prioritized **any-to-any command routing**.

### Interfaces Mentioned
- LoRa
- BLE
- WiFi
- ESP-NOW
- MQTT
- Serial
- Local AI (via ASK command routing over Serial)

### Command Routing Principle
Messages can enter from one interface and be routed to another based on addressing / target logic managed by `CommandManager`. For example, `ASK <prompt>` forwards mesh queries directly to the Local AI Workstation over Serial.

### Agent Audit Goals
Whenever analyzing the routing system, determine:
- all `CommInterface` values
- where commands enter from each transport
- how local vs forwarded commands are distinguished
- what reply path is used
- where target prefixes are parsed

---

## 6. PC-Side Tooling Is First-Class

The `tools/` directory is part of the real product surface.

### Key Tools
- `tools/ble_instrument.py`
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`
- `tools/requirements.txt`

### Important Rule
Any firmware change affecting:
- commands
- API endpoints
- pin aliases
- limits
- schedule task types
- BLE UUID / notify behavior
- ESP-NOW peer/public behavior
must update the corresponding tool files in the same change.

### Why This Matters
Firmware and tools can silently drift unless treated as a single coupled system.

---

## 7. Known Coupling Map

### If `src/managers/CommandManager.cpp` changes
Also inspect:
- `tools/ble_instrument.py`
- `tools/webapp/static/index.html`

### If `src/managers/WiFiManager.cpp` changes
Also inspect:
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`

### If `src/managers/ScheduleManager.h` changes
Also inspect:
- `tools/ble_instrument.py`
- `tools/webapp/static/index.html`

### If `src/config.h` changes
Also inspect:
- `tools/ble_instrument.py`
- `tools/webapp/static/index.html`

### If `src/managers/BLEManager.cpp` changes
Also inspect:
- `tools/ble_instrument.py`
- `tools/webapp/server.py`

### If `src/managers/ScheduleManager.cpp` changes
Also inspect:
- `tools/webapp/static/index.html`
- `tools/ble_instrument.py`

### If `src/managers/ESPNowManager.cpp` or `.h` changes
Also inspect:
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`

### If `src/managers/MCPManager.cpp` or `.h` changes
Also inspect:
- `src/managers/CommandManager.cpp`
- `src/managers/ScheduleManager.cpp`

---

## 8. Hardware / Board-Specific Facts

### Current Board
Only Heltec WiFi LoRa 32 V3 should be treated as implemented.

### OLED / Power
- OLED is I2C-based
- `VEXT` pin 36 must be driven `LOW` to provide power

### LoRa / Pin Conflict
- Pin 14 is shared by:
  - `PIN_RELAY_12V_1`
  - `LORA_DIO1`

**Never enable both.**

### Why This Matters
Any refactor around board abstraction must preserve these constraints and keep them explicit.

---

## 9. Coding Rules / Repo Rules

### Coding
- indentation: 2 spaces
- global/system headers use `<>`
- project-relative headers use `""`
- `CommInterface` values should use `COMM_` prefix

### Git / Workflow
- repo lives in `spw1` monorepo
- default branch: `main`
- feature work on `feature/<topic>`
- always PR to `main`
- never commit directly to `main`

### OTA
- uses mDNS
- no hardcoded IPs

### Versioning
- manual
- update `FIRMWARE_VERSION` in `src/config.h` only for meaningful releases

### Build Strategy
- single build
- multi-flash deployment
- do not build separate firmware per device unless explicitly justified

---

## 10. Future Architectural Direction

The preferred evolution path is:

1. **Board Support Abstraction**
   - isolate Heltec-specific assumptions
   - keep Heltec as only implemented board

2. **Device Classes**
   - Gateway
   - Messenger
   - Sensor Node
   - Repeater
   - Actuator
   - DevKit

3. **Feature Profiles**
   - modular transport stacks
   - selected per logical role

4. **Operating Modes**
   - SETUP
   - NORMAL
   - DIAGNOSTIC
   - LOW_POWER
   - SAFE_MODE

5. **Command Domains**
   - system
   - network
   - hardware
   - scheduler
   - diagnostic
   - messaging

6. **Development-Only Diagnostics**
   - rich OLED/serial/MQTT diagnostics only in development or explicit diagnostic mode

### Important Constraint
The system should evolve **incrementally**. Do not assume a rewrite.

---

## 11. What an Agent Should Do First

Before suggesting architecture or code changes, an agent should:

1. Read `main.cpp`
2. Read `src/config.h`
3. Inspect all files in `src/managers/`
4. Identify every registered command
5. Identify all routes/endpoints in `WiFiManager`
6. Identify all transport entry points into `CommandManager`
7. Inspect `tools/ble_instrument.py`
8. Inspect `tools/webapp/server.py`
9. Inspect `tools/webapp/static/index.html`

Only after that should it propose changes.

---

## 12. Recommended Response Shape for Agents

When analyzing the repo, structure findings as:

- **Observed in Code**
- **Inferred Behavior**
- **Architectural Risks**
- **Recommended Changes**
- **Files Affected**
- **Tool Coupling**
- **Validation Plan**

This keeps analysis grounded and useful.

---

## 13. Final Guiding Principle

This project is best understood as:

**a Heltec-first embedded command-and-automation platform with multiple transports, centralized command routing, scheduled runtime behavior, and tightly coupled PC-side tooling.**

Agents should preserve that truth while helping the codebase evolve safely.
