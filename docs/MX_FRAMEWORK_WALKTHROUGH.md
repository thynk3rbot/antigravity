# Mx Framework Implementation Walkthrough

The **Mx framework** is a unified internal message bus, last-value cache (LVC), and active-object infrastructure designed to modernize the Magic IoT mesh communications. This document walkthroughs the implementation details of Phase 1 (Core) and Phase 2 (WiFi Adapter).

---

## 1. Core Framework (lib/Mx)

The core framework was built in **C++ (Firmware)** and **Python (Daemon)** to ensure seamless interoperability.

### Static Memory Management
To guarantee system stability on the ESP32, all message memory is managed via a static pool:
- **MxPool**: Holds 16 pre-allocated slots for `MxMessage` objects.
- **MxQueue**: Uses FreeRTOS under the hood to carry 4-byte pointers to these slots, ensuring that the "Active Object" tasks can receive messages without heap allocation or fragmentation.

### Low-Bandwidth Record Sync (LVC)
The `MxRecord` structure uses a `dirty_mask` (bitmask). When a field (e.g., Battery Voltage) is changed, only that field's index is marked. During transmission, the framework only serializes fields marked in the mask.

---

## 2. WiFi Active Object (WiFiMxAdapter)

In Phase 2, we converted the `WiFiTransport` into an **Active Object**. This is a major stability improvement.

### The Problem: WDT Resets
Old code called `WiFi.send()` or `ArduinoOTA.handle()` from multiple different tasks. This led to lock contention in the ESP32 WiFi stack, sometimes blocking for 15+ seconds and triggering the Task Watchdog (WDT).

### The Solution: Queue-Based Processing
1. **MxBus Dispatching**: When a command arrives, the system publishes it to the `MxBus`. 
2. **WiFi Queue**: The `WiFiMxAdapter::consume()` method catches the message and immediately posts it to a dedicated `wifi` queue (non-blocking).
3. **Sequential Execution**: Inside the `wifiTask` loop (created in `main.cpp`), we call `adapter.drainQueue()`. This task owns the WiFi stack locks. It processes up to 4 messages at a time, ensuring it doesn't starve the OTA/mDNS system.

---

## 3. Wire Protocol (MxWire)

The binary proto format is optimized for LoRa frames (max 256 bytes):
- **Header (8 bytes)**: Operations, Node ID, Subject ID, Sequence, TTL, Payload Length.
- **Payload (N bytes)**: LVC-packed fields.
- **CRC-16 (2 bytes)**: Ensures data integrity over long-range wireless.

---

## 4. Verification Results

### Built-in Tests
- **Daemon Smoke Test**: verified that the Python `MxBus` correctly routes messages to an async consumer.
- **PlatformIO Build**: `pio run -e heltec_v4` confirms that the entire suite compiles on the production hardware target with strict C++17 rules.

---

**Handover Notes:**
- All framework files live in `lib/Mx/`.
- Integrating a new manager (e.g., GPS) should follow the same adapter pattern as `WiFiMxAdapter`.
- The framework is currently **Enabled** but **Unwired** from legacy LoRa logic to allow for a staggered release.
