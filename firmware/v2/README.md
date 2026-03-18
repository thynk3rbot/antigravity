# LoRaLink v2 - Lightweight Multi-Transport Mesh & Environment Controller

**Version:** 0.2.0
**Status:** Architecture & Headers Complete (Implementation in Progress)

LoRaLink v2 is a decentralized IoT mesh networking system for ESP32-based controllers with multi-hop LoRa communication, local relay control, and optional cloud bridging.

## Features

### Core Mesh
- **Multi-Hop LoRa Mesh:** SX1276 (V2 Hub) + SX1262 (V3/V4 Nodes)
- **Encrypted Communication:** AES-128-GCM on all packets
- **Packet Deduplication:** Rolling hash buffer prevents relay loops
- **Greedy Routing:** Hops selected by best RSSI + lowest hop-count

### Hardware Support
- **3 Board Variants:** Heltec V2, V3, V4 (compile-time validation)
- **GPIO Relay Control:** 8 relay channels per node (via bitmask)
- **Telemetry Collection:** Battery voltage, temperature, signal strength
- **Power Management:** 3-tier battery modes (Normal, Conserve, Critical)

### Transports (Pluggable)
- **LoRa:** 1-10 km range, default transport
- **MQTT (Hub):** WiFi bridge to cloud
- **Serial (Optional):** 115200 baud CLI debug
- **BLE (Future):** V4 with PSRAM support

## Hardware Stack

| Board | MCU | Radio | USB | PSRAM | Role |
|-------|-----|-------|-----|-------|------|
| Heltec V2 | ESP32 | SX1276 | CP2102 | None | Hub |
| Heltec V3 | ESP32-S3 | SX1262 | CP2102 | None | Node |
| Heltec V4 | ESP32-S3R2 | SX1262 | Native | 2 MB | Node |

## Quick Start

### Build for Hub (V2)
```bash
cd loralink-v2
platformio run --environment heltec_v2_hub
platformio run --environment heltec_v2_hub --target upload
```

### Build for Node (V3)
```bash
platformio run --environment heltec_v3_node
platformio run --environment heltec_v3_node --target upload
```

### Build for Node (V4 with Native USB)
```bash
platformio run --environment heltec_v4_node
platformio run --environment heltec_v4_node --target upload
```

### Serial Monitor
```bash
platformio device monitor --baud 115200
```

### Bench Mode (Optional Hardware Diagnostics)
Enable diagnostic testing with `-D BENCH_MODE`:
```bash
# Edit platformio.ini and add -D BENCH_MODE to build_flags
platformio run --environment heltec_v2_hub
```
See **BENCH_MODE.md** for complete diagnostic testing guide.

## Architecture

```
src/main.cpp (FreeRTOS Setup)
    ├── radioTask (Core 0, Priority 3) ─→ LoRa RX polling
    └── controlTask (Core 1, Priority 2) ─→ Relay commands, telemetry

                ↓ Message Flow ↓

lib/Transport/message_router.h
    ├── Polls all transports
    ├── Routes packets between transports
    └── Calls application message handlers

                ↓

lib/Transport/lora_transport.h (+ MQTT, BLE, Serial)
    ├── Encryption (AES-128-GCM)
    ├── Deduplication
    └── RSSI tracking

                ↓

lib/App/control_packet.h (14-byte packets)
    ├── TELEMETRY (node telemetry)
    ├── ACTION (relay commands)
    ├── ACK (reliability)
    └── HEARTBEAT (mesh discovery)

                ↓

lib/App/mesh_coordinator.h
    ├── Neighbor tracking
    ├── Relay decisions
    └── Multi-hop routing

                ↓

lib/HAL/ (board_config.h, radio_hal.h, relay_hal.h)
    ├── GPIO pin assignments
    ├── SX1276/SX1262 abstraction
    └── Relay state machine
```

## Project Structure

```
loralink-v2/
├── src/
│   └── main.cpp                    # FreeRTOS entry point, tasks
├── lib/
│   ├── HAL/
│   │   ├── board_config.h         # Pins, build-flag validation
│   │   ├── radio_hal.h            # LoRa radio abstraction
│   │   └── relay_hal.h            # Relay GPIO control
│   ├── Transport/
│   │   ├── interface.h            # Abstract transport base
│   │   ├── lora_transport.h       # LoRa implementation
│   │   ├── message_router.h       # Any-to-any routing
│   │   └── (future: mqtt, ble, serial)
│   ├── App/
│   │   ├── control_packet.h       # Binary protocol (14B)
│   │   ├── mesh_coordinator.h     # Topology + routing
│   │   └── (future: telemetry, persistence)
│   └── README.md                   # Library documentation
├── include/                         # (empty, lib/ is primary)
├── test/                            # Unit tests (future)
├── 01_planning/
│   └── spec.md                     # Complete technical spec (v1 carryover included)
├── 02_coding/                       # Implementation phase
├── 03_review/                       # Code review phase
├── platformio.ini                  # Build config (3 environments)
└── README.md                        # This file
```

## Key Files & Responsibilities

| File | Lines | Purpose |
|------|-------|---------|
| **board_config.h** | 250 | GPIO pins, compile-time guards |
| **radio_hal.h** | 180 | SX1276/SX1262 abstraction |
| **relay_hal.h** | 150 | Relay state + GPIO |
| **interface.h** | 140 | Transport base class |
| **lora_transport.h** | 160 | Encrypted LoRa + dedup |
| **message_router.h** | 200 | Packet routing singleton |
| **control_packet.h** | 280 | Binary protocol structs |
| **mesh_coordinator.h** | 250 | Routing + neighbor tracking |
| **main.cpp** | 350 | FreeRTOS tasks, boot sequence |

## Build Flags & Validation

All builds use **three environments** with strict compile-time validation:

```ini
[env:heltec_v2_hub]
build_flags = -D ROLE_HUB -D RADIO_SX1276

[env:heltec_v3_node]
build_flags = -D ROLE_NODE -D RADIO_SX1262

[env:heltec_v4_node]
build_flags = -D ROLE_NODE -D RADIO_SX1262 -D BOARD_HAS_PSRAM -D ARDUINO_USB_MODE=1
```

`board_config.h` validates:
- `ROLE_HUB` + `ROLE_NODE` = **CONFLICT** (compile error)
- `ROLE_HUB` + `RADIO_SX1262` = **MISMATCH** (compile error)
- `BOARD_HAS_PSRAM` without `-D ARDUINO_USB_MODE=1` = **INVALID** (compile error)

## v1 Carryover Checklist

From LoRaLink v0.1.0, the following **critical features** are inherited:

- ✅ **AES-128-GCM encryption** (moved to transport layer)
- ✅ **Packet deduplication** (rolling hash in MessageRouter)
- ✅ **Reliable delivery** (pending-ACK queue in LoRaTransport)
- ✅ **Repeater/relay logic** (MeshCoordinator::shouldRelay)
- ✅ **3-tier power modes** (Battery NORMAL/CONSERVE/CRITICAL)
- ✅ **Telemetry collection** (ADC, RSSI, uptime)
- ✅ **MQTT hub bridge** (Transport layer, Hub-only)
- ✅ **OTA capability** (Arduino OTA, optional)
- ✅ **OLED display** (status UI, 4-page navigation)

**Simplified vs v1:**
- FreeRTOS tasks instead of TaskScheduler
- No ESP-NOW (LoRa focus)
- No remote node log (NVS trimmed to essentials)
- No dynamic pin naming (compile-time names only)
- CLI deferred to v2.1 (MVP = OLED + MQTT only)

## Implementation Status

| Component | Status | Notes |
|-----------|--------|-------|
| **Headers** | ✅ Complete | All core definitions written |
| **platformio.ini** | ✅ Complete | 3 environments configured |
| **board_config.h** | ✅ Complete | Pin mapping + validation |
| **HAL stubs** | ⏳ Next | radio_hal.cpp, relay_hal.cpp |
| **Transport stubs** | ⏳ Next | lora_transport.cpp, message_router.cpp |
| **App logic** | ⏳ Next | mesh_coordinator.cpp, telemetry |
| **Main loop** | ⏳ Next | Task implementations |
| **Testing** | ⏳ Future | Unit tests, integration tests |
| **Docs** | ⏳ Future | API docs, user guide |

## Next Steps

1. **Implement HAL stubs** (radio_hal.cpp, relay_hal.cpp)
2. **Implement Transport** (lora_transport.cpp, message_router.cpp)
3. **Implement App logic** (mesh_coordinator.cpp, telemetry_collector.cpp)
4. **Compile & test** on hardware (V2, V3, V4)
5. **Add serial CLI** transport (v2.1)
6. **Add MQTT bridge** (Hub-only, v2.1)
7. **Add OTA updates** (v2.2)

## Technical Reference

See **`01_planning/spec.md`** for:
- Complete technical specification
- Packet format details
- Mesh routing algorithm
- Power management scheme
- v1 carryover strategy
- Future extensions

## Dependencies

```ini
RadioLib @ 6.4.0
Adafruit SSD1306 @ 2.5.0
Adafruit GFX Library @ 1.11.0
PubSubClient @ 2.8
(+ FreeRTOS, Arduino ESP32 core, built-in)
```

## License & Attribution

LoRaLink v2 is the next generation of the v0.1.0 firmware, redesigned for **clean architecture, strict HAL isolation, and multi-board support**.

Key design philosophy:
- **Compile-time safety:** Invalid permutations caught at build, not runtime
- **Abstraction:** Transport-agnostic, radio-agnostic, board-agnostic
- **Efficiency:** Packed structs, no overhead, minimal dependencies
- **Reliability:** AES encryption, packet dedup, multi-hop redundancy
