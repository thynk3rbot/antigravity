# Magic v2 Library Structure

This directory contains the core embedded firmware libraries organized in three layers: **HAL** (Hardware Abstraction), **Transport**, and **App** (Application).

## Directory Layout

```
lib/
├── HAL/                      # Hardware Abstraction Layer
│   ├── board_config.h        # Pin mappings, compile-time validation
│   ├── radio_hal.h           # LoRa radio (SX1276/SX1262) abstraction
│   └── relay_hal.h           # GPIO relay control
├── Transport/                # Communication Transport Layer
│   ├── interface.h           # Abstract transport interface base class
│   ├── lora_transport.h      # LoRa implementation (encrypted, deduped)
│   ├── message_router.h      # Any-to-any message dispatch singleton
│   ├── mqtt_transport.h      # MQTT (Hub-only, future)
│   └── serial_transport.h    # Serial CLI (optional, future)
├── App/                      # Application Layer
│   ├── control_packet.h      # Binary protocol packet definitions
│   ├── mesh_coordinator.h    # Multi-hop mesh topology manager
│   ├── telemetry_collector.h # ADC sampling, temperature reading (future)
│   └── nvs_schema.h          # NVS persistence definitions (future)
└── README.md                 # This file
```

## Layer Responsibilities

### HAL (Hardware Abstraction)
- **board_config.h**: Centralizes GPIO pins, compile-time validation. Prevents invalid V2/V3/V4 permutations via `#error` directives.
- **radio_hal.h**: Wraps RadioLib to abstract SX1276 (V2) vs SX1262 (V3/V4). Provides async RX/TX, diagnostics, spectrum scanning.
- **relay_hal.h**: GPIO relay control with 8-channel bitmask state management. Supports future MCP23017 GPIO expander integration.

### Transport
- **interface.h**: Abstract base class (`TransportInterface`) that all transports inherit from. Defines send/recv contract, status, diagnostics.
- **lora_transport.h**: Implements encrypted (AES-128-GCM), deduplicated LoRa communication. Integrates packet deduplication, RSSI tracking.
- **message_router.h**: Singleton that polls all registered transports, dispatches packets to handlers, bridges transports. Central nervous system of the firmware.
- **mqtt_transport.h**: Future hub-only feature for cloud integration.
- **serial_transport.h**: Future debug CLI (115200 baud serial protocol).

### App
- **control_packet.h**: Binary protocol packet struct (14 bytes total). Packed C-struct (no Protocol Buffers) with PacketHeader (6B) + Payload union (8B). Fits in single LoRa frame.
  - PacketType: TELEMETRY, ACTION, ACK, HEARTBEAT, MESH_PROBE
  - Factory methods: `makeTelemetry()`, `makeAction()`, `makeACK()`
- **mesh_coordinator.h**: Manages neighbor discovery, hop-count tracking, relay decisions. Greedy routing (best RSSI + lowest hops). Prevents relay loops via sequence deduplication.
- **telemetry_collector.h**: ADC sampling (battery voltage, temperature). Exposed via API for on-demand or periodic collection.
- **nvs_schema.h**: NVS key names and structure definitions for persistent settings (crypto key, node ID, link preference).

## Build Configuration

All three environments (`heltec_v2_hub`, `heltec_v3_node`, `heltec_v4_node`) define build flags that `board_config.h` uses:

```ini
-D ROLE_HUB / ROLE_NODE        # Functional role
-D RADIO_SX1276 / RADIO_SX1262 # Radio model (validated vs board)
-D ARDUINO_HELTEC_WIFI_LORA_32 # Board variant (V2, V3, or V4)
-D BOARD_HAS_PSRAM             # V4 only (enables large buffers)
```

If flags mismatch (e.g., `ROLE_NODE` + `RADIO_SX1276`), compilation fails with `#error` at the `board_config.h` level.

## Typical Compilation Flow

1. **platformio.ini** specifies environment (e.g., `heltec_v3_node`)
2. **Build flags** injected (e.g., `-D ROLE_NODE -D RADIO_SX1262`)
3. **board_config.h** validates flags; assigns GPIO pins
4. **main.cpp** includes HAL/Transport/App layers
5. Firmware compiled with correct pinout, no runtime overhead

## Key Design Patterns

### Singleton Pattern
All managers (RadioHAL, RelayHAL, MessageRouter, MeshCoordinator) expose `getInstance()` for safe global access without static initialization issues.

### Transport Abstraction
All transport types (LoRa, MQTT, Serial, BLE) inherit from `TransportInterface`, enabling runtime binding via `messageRouter.registerTransport()`.

### Packed C-Struct Protocol
14-byte fixed-size packets (no variable-length encoding, no Protocol Buffers). Direct memory mapping for efficiency.

```cpp
ControlPacket pkt = ControlPacket::makeTelemetry(...);
messageRouter.broadcastPacket((uint8_t*)&pkt, sizeof(pkt));
```

### Compile-Time vs Runtime
- **Compile-time**: Pin mapping, role (Hub/Node), radio model
- **Runtime**: Message dispatch, mesh neighbor discovery, relay decisions

## Memory Footprint

| Component | Approx. Size | Notes |
|-----------|--------------|-------|
| board_config.h | 1 KB | Headers only, no code |
| radio_hal.h | ~4 KB | Includes RadioLib (external) |
| relay_hal.h | ~2 KB | GPIO state machine |
| Transport layer | ~6 KB | Interface + LoRa impl |
| control_packet.h | <1 KB | Struct definitions |
| mesh_coordinator.h | ~4 KB | Neighbor map, routing logic |
| **Total (headers only)** | **~17 KB** | Pre-linking |
| **Runtime (linked + RadioLib)** | **~200-250 KB** | Varies by board |

V4 with PSRAM (2 MB) can support future large buffers (e.g., telemetry ring, OTA staging).

## Future Extensions

- **Serial CLI Transport**: 115200 baud command interface (debug)
- **MQTT Transport (Hub)**: WiFi bridge to cloud
- **BLE Transport (V4)**: GATT server for mobile apps
- **Telemetry Ring Buffer**: Circular on-disk storage (LittleFS)
- **OTA Updates**: Over-the-air firmware patching via LoRa mesh
- **Dynamic Topology Discovery**: MESH_PROBE beacon + neighbor learning

## Related Files

- `src/main.cpp` - FreeRTOS task setup, entry point
- `01_planning/spec.md` - Complete technical specification
- `platformio.ini` - Build configuration (3 environments)
