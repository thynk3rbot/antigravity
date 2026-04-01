# Threading Model — firmware/v2

**Last updated:** 2026-04-01
**Status:** Active — WDT stability fix landed in 0.0.22, SEF queue model planned

---

## Why This Document Exists

Threading decisions are the hardest to reverse and the most expensive to rediscover.
Bad threading choices manifest as intermittent crashes, not compile errors — they take
hours to diagnose and minutes to fix once you understand them. This document captures
the constraints, the mistakes made, the fixes applied, and the intended direction so
no future session starts from zero.

---

## Current Model (FreeRTOS SMP, as of 0.0.22)

The ESP32-S3 has two cores. FreeRTOS runs symmetrically across both. Tasks are pinned
at creation time.

### Task Map

| Task | Core | Priority | Stack | Period | Responsibility |
|------|------|----------|-------|--------|----------------|
| `RadioTask` | 0 | 4 (high) | 8192 | 100ms wait | LoRa RX, ISR notify, packet relay |
| `MeshTask` | 0 | 3 | 8192 | 500ms | Neighbor aging, topology |
| `ProbeTask` | 0 | 2 | 4096 | 10ms | WiFi promiscuous / BLE scan (when enabled) |
| `ControlLoop` | 1 | 2 | 4096 | 10ms (100Hz) | Power, telemetry, OLED data, scheduler, beacons, plugins |
| `DisplayTask` | 1 | 1 | 4096 | 100ms | I2C buffer flush to OLED (deferred from ControlLoop) |
| `WiFiTask` | 1 | 1 | 4096 | 50ms | ArduinoOTA.handle(), WiFi reconnect, MQTT poll |
| `loop()` | 1 | 1 | Arduino | 10ms | Serial CLI, periodic JSON status |
| `async_tcp` | 0/1 | 3 (ESP-IDF) | system | event-driven | ESPAsyncWebServer TCP handler |
| `mdns` | system | system | system | background | mDNS advertisement |

### Synchronization

There is **one explicit mutex** in application code: `g_i2cMutex` protecting the shared
I2C bus (SDA=17, SCL=18) used by both OLED and MCP23017. All other inter-task
communication is through direct singleton method calls — there is no message queue
between tasks at the application layer.

This means: any task calling a singleton that internally calls WiFi, NVS, or I2C must
be considered a potential blocking point.

---

## Known Failure Mode: async_tcp WDT (fixed in 0.0.22)

### What happened

Devices rebooted periodically, approximately 60 seconds after boot. Serial output:

```
E (61514) task_wdt:  - async_tcp (CPU 0/1)
E (61514) task_wdt: CPU 0: mdns
E (61514) task_wdt: CPU 1: IDLE1
E (61514) task_wdt: Aborting.
abort() was called at PC 0x42023ad4 on core 0
```

### Root cause

`async_tcp` is the internal FreeRTOS task created by ESPAsyncWebServer. It
self-registers with the ESP-IDF Task Watchdog (TWDT). The default TWDT timeout is
**5 seconds**.

`WiFiTask` calls `ArduinoOTA.handle()` every 50ms. ArduinoOTA internally drives mDNS
processing which acquires WiFi stack internal locks. The ESP-IDF WiFi stack is **not
fully re-entrant** — simultaneous access from `async_tcp` (handling an HTTP request)
and `WiFiTask` (driving mDNS) can cause one to block waiting for the other's lock
release. When this contention exceeded 5 seconds, TWDT fired.

### Why it took ~60 seconds to appear

mDNS advertisement ramps up after WiFi association completes and services are
registered. Lock contention only occurs when an HTTP client connects. In practice
this happened on the first web dashboard poll after connectivity stabilized.

### Fix applied

1. `boot_sequence.cpp`: `esp_task_wdt_init(15, true)` — extends TWDT to 15 seconds
   before `HttpAPI::init()` starts the async TCP server. `async_tcp` inherits this
   timeout when it self-registers.

2. `main.cpp` / `WiFiTask`: `esp_task_wdt_reset()` called after `ArduinoOTA.handle()`
   on each loop iteration. This feeds the watchdog for `WiFiTask` itself and signals
   the system that the task is alive during OTA/mDNS work.

### What 15 seconds buys

It is long enough to tolerate brief WiFi stack lock contention under normal operation.
It is short enough to catch a genuine deadlock (which would not resolve on its own).
The correct long-term fix is the SEF queue model below — which eliminates this entire
class of problem by removing direct cross-task calls.

---

## Known Failure Mode: NVS Erase on Reflash (fixed in 0.0.22)

### What happened

After every firmware flash, WiFi credentials, device name, and relay state were lost.

### Root cause

`NVSManager::init()` called `nvs_flash_erase()` when `ESP_ERR_NVS_NEW_VERSION_FOUND`
was returned. This error fires on **every new firmware version** because the NVS
library embeds a version header. The error does **not** mean the data is corrupt —
it means the library version in the new firmware differs from the one that wrote the
data. The key/value data is fully readable. Erasing was a copy-paste from Espressif's
"clean slate" example code, not a correctness requirement.

### Fix applied

`ESP_ERR_NVS_NEW_VERSION_FOUND` now logs a warning and returns `ESP_OK`. Only
`ESP_ERR_NVS_NO_FREE_PAGES` (genuine partition full) triggers an erase, since that
is the only case where erasure is the only recovery path.

---

## Stack Size Rationale

Stack sizes in `createTasks()` are in **bytes** (Arduino ESP32 framework convention).

`ControlLoop` and `WiFiTask` were originally allocated 4096 bytes — half the size of
`RadioTask` (8192) despite doing significantly more work per tick. This is a known
risk: stack overflow on ESP32 manifests as a silent memory corruption or WDT reset,
not a clean exception. **If unexplained reboots return, increase `ControlLoop` to
8192 and `WiFiTask` to 6144 as the first diagnostic step.**

The `ProbeTask` at 4096 bytes is also tight if WiFi promiscuous + NimBLE scanning are
active simultaneously. If `ProbeManager::setSniffing(true)` is used, monitor stack
high-water mark with `uxTaskGetStackHighWaterMark(probeTaskHandle)`.

---

## ESP32-S3 Threading Constraints

These are hard constraints that any future threading model must respect:

| Constraint | Detail |
|---|---|
| **WiFi API is not thread-safe** | `WiFi.*`, `MDNS.*`, `ArduinoOTA.*` must not be called from two tasks simultaneously without external locking |
| **I2C is not thread-safe** | OLED and MCP23017 share SDA=17/SCL=18. Always acquire `g_i2cMutex` before any `Wire.*` or `display.*` call |
| **ISR stack is separate and small** | No heap allocation, no `std::string`, no `Serial.print` in ISR context. RadioLib's `setPacketReceivedAction` callback fires in ISR |
| **NVS is not ISR-safe** | All NVS reads/writes must happen in task context only |
| **FreeRTOS heap is shared** | `new`/`malloc` compete across all tasks. Fragmentation is a real risk with long uptimes and dynamic transports (BLE, MQTT) |
| **vTaskDelay(0) is not a yield** | Use `taskYIELD()` or `vTaskDelay(1)` for cooperative yield |
| **Pin 14 is LORA_DIO1** | Never assign as GPIO output on V3/V4. Shared with relay on V2 hardware |

---

## Intended Direction: SEF Queue Model

The current direct-singleton model works but carries structural debt: any task can
block any other task by calling a shared resource at the wrong moment. The WDT crash
above is one example; I2C glitches from concurrent access are another.

The intended replacement is a **queue-based active object model** using the owner's
SEF (SoftCache) framework — an industrial-strength C++ threading model with a proven
production history. Under this model:

- Each manager becomes an **active object**: it owns a private task and an input queue
- Callers **post messages** to the queue rather than calling methods directly
- No shared mutable state across tasks — all state changes are serialized through the queue
- Lock contention becomes impossible by design rather than managed by convention

### Mapping to current managers

| Current singleton | Queue model role |
|---|---|
| `LoRaTransport` | Active object, high-priority queue, ISR posts RX notifications |
| `WiFiTransport` / mDNS / OTA | Active object, low-priority queue, eliminates async_tcp contention |
| `OLEDManager` | Active object, display update queue, no more I2C mutex needed |
| `MeshCoordinator` | Active object, neighbor event queue |
| `CommandManager` | Dispatcher, routes messages from any queue to any other |
| `NVSManager` | Synchronous (NVS is already serialized internally by ESP-IDF) |

### ESP32-S3 compatibility requirements for SEF

Before integration, verify SEF against:
- [ ] No `std::thread` or `pthread` — must use `xTaskCreate` or wrap it
- [ ] Static or pool allocation — no unbounded heap growth per message
- [ ] Atomic queue operations safe across dual cores (or wraps FreeRTOS queues)
- [ ] No OS-specific blocking primitives in queue post path (must be ISR-safe for RadioTask)
- [ ] C++17 compatible (firmware builds with `-std=c++17`)

---

## Diagnostic Commands

To inspect task state at runtime via serial monitor:

```
STATUS          — shows uptime, heap, transport states, reset reason, boot count
GETCONFIG       — shows all NVS-persisted values
```

To add stack high-water mark logging (not currently in firmware, add to `ControlLoop::updateStatusRegistry`):
```cpp
Serial.printf("[STACK] Radio=%u Mesh=%u Control=%u WiFi=%u\n",
  uxTaskGetStackHighWaterMark(radioTaskHandle),
  uxTaskGetStackHighWaterMark(meshTaskHandle),
  uxTaskGetStackHighWaterMark(controlTaskHandle),
  uxTaskGetStackHighWaterMark(NULL)); // current task
```

---

## Revision History

| Version | Date | Change |
|---------|------|--------|
| 0.0.22V3/V4 | 2026-04-01 | async_tcp WDT fix (15s timeout + WiFiTask feed); NVS preserve on reflash |
| 0.0.21V3/V4 | 2026-04-01 | esp_reset_reason() captured at boot; relay mask persisted to NVS; OLED 30s timeout fixed |
| 0.0.17V3 | prior | baseline — no threading documentation |
