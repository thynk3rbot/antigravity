# Mx Phase 2 — Wire WiFiTransport to MxBus

**Date:** 2026-04-01
**Prerequisites:** `git pull origin main` — Mx framework is in `firmware/magic/lib/Mx/`
**Goal:** Make WiFiTransport the first active object — eliminate the async_tcp WDT crash class permanently.

---

## PROMPT START

You are wiring the first existing manager to the Mx message bus framework. The Mx framework is already implemented and compiles — do NOT modify any file in `firmware/magic/lib/Mx/`. You are creating **adapter code** that bridges the existing `WiFiTransport` to `MxBus`.

### Background — Why WiFiTransport First

The firmware has a known failure mode: `async_tcp` (ESPAsyncWebServer's internal task) shares WiFi stack locks with `WiFiTask` (which runs ArduinoOTA + mDNS). When lock contention exceeds 15 seconds, the Task Watchdog fires and the device reboots. The current fix is a band-aid (extended WDT timeout to 15s).

The real fix: WiFiTransport becomes an **active object** — it owns an MxQueue and processes messages sequentially on its own task. No other task calls WiFiTransport methods directly. Lock contention becomes impossible because all WiFi work runs on one task.

### What You're Building

**One new file:** `firmware/magic/lib/App/wifi_mx_adapter.h` (and `.cpp` if needed)

This adapter:
1. Creates an `MxQueue` named `"wifi"` with depth 8
2. Implements `MxConsumer` — receives messages from MxBus
3. On `MxOp::EXECUTE` with subject `MX_SUBJ_COMMAND` — routes to existing WiFi command handlers
4. On `MxOp::UPDATE` with subject `MX_SUBJ_NODE_STATUS` — triggers HTTP status endpoint update
5. Runs its queue drain loop inside the existing `WiFiTask` (in `main.cpp`)

### Integration Points — Existing Code (READ, do not modify structure)

```
firmware/magic/lib/Transport/wifi_transport.h   — WiFiTransport class
firmware/magic/lib/Transport/wifi_transport.cpp  — init(), connect(), send()
firmware/magic/src/main.cpp                      — WiFiTask function (line ~180)
firmware/magic/lib/App/command_manager.h         — CommandManager routes commands
firmware/magic/lib/App/boot_sequence.cpp         — initTransports() creates WiFiTransport
```

Read ALL of these before writing any code.

### Architecture

```
Before (direct calls, lock contention possible):
  CommandManager::route() → WiFiTransport::send()     ← blocks on WiFi lock
  WiFiTask loop           → ArduinoOTA.handle()       ← holds WiFi lock
  async_tcp               → ESPAsyncWebServer handler  ← needs WiFi lock → WDT

After (queue-based, no contention):
  CommandManager::route() → MxBus::publish(COMMAND)    ← returns immediately
  MxBus                   → WiFiMxAdapter queue        ← post, non-blocking
  WiFiTask loop           → adapter.drainQueue()       ← processes sequentially
                          → ArduinoOTA.handle()        ← same task, no lock fight
```

### Code Structure

```cpp
// firmware/magic/lib/App/wifi_mx_adapter.h
#pragma once
#include "mx_bus.h"
#include "mx_consumer.h"
#include "mx_queue.h"
#include "mx_subjects.h"

class WiFiMxAdapter : public MxConsumer {
public:
    static WiFiMxAdapter& instance();

    void init();          // Create queue, subscribe to bus
    void drainQueue();    // Call from WiFiTask loop — processes up to N messages per tick

    // MxConsumer interface
    void consume(const MxMessage& msg) override;

private:
    WiFiMxAdapter() = default;
    MxQueue m_queue{"wifi", 8};
};
```

### Implementation Rules

```cpp
// In WiFiMxAdapter::init():
//   Subscribe to MX_SUBJ_COMMAND and MX_SUBJ_NODE_STATUS on MxBus
//   Do NOT subscribe to subjects this adapter doesn't handle

// In WiFiMxAdapter::consume():
//   This is called BY MxBus::publish() — it runs on the CALLER's task
//   It MUST only post to m_queue and return immediately
//   It MUST NOT call any WiFi API, Serial, or I2C from here
//   It MUST NOT block

// In WiFiMxAdapter::drainQueue():
//   Called from WiFiTask loop (same task that does ArduinoOTA)
//   Process up to 4 messages per call (don't starve OTA/mDNS)
//   For each message:
//     - MxOp::EXECUTE → call existing WiFiTransport command handler
//     - MxOp::UPDATE  → update cached status for next HTTP GET
//     - Release the message back to pool: m_queue.release(msg)
//   Return after 4 messages OR queue empty, whichever comes first
```

### ESP32/FreeRTOS Safety Checklist — MANDATORY

Before submitting, verify EVERY item:

- [ ] **`consume()` only posts to queue** — no WiFi calls, no Serial, no I2C, no NVS
- [ ] **`consume()` does not block** — `m_queue.post()` returns immediately (0 tick timeout)
- [ ] **No `portMAX_DELAY`** in any `pdMS_TO_TICKS()` call — use raw `portMAX_DELAY` constant if blocking forever
- [ ] **No `std::string` construction** in any path callable from ISR
- [ ] **`drainQueue()` limits work per call** — max 4 messages, then return to let OTA/mDNS run
- [ ] **`m_queue.release(msg)`** called for EVERY message after processing — pool slots are finite (16 total)
- [ ] **No `new` or `malloc`** in any hot path — all allocation comes from MxPool
- [ ] **Thread safety of `init()`** — only call from `boot_sequence.cpp` before tasks start, or protect with a flag
- [ ] **No modification to existing files** except adding `adapter.drainQueue()` call in WiFiTask loop

### What NOT To Do

1. **Do NOT modify `WiFiTransport`** — the adapter wraps it, doesn't replace it
2. **Do NOT modify `CommandManager`** — Phase 2 only adds the adapter; CommandManager still calls transports directly for now. Phase 3 will route through MxBus.
3. **Do NOT modify any Mx framework files** — they are reviewed and correct
4. **Do NOT add a new FreeRTOS task** — reuse WiFiTask
5. **Do NOT use `std::function` or lambdas for callbacks** — use the MxConsumer interface
6. **Do NOT catch exceptions** — ESP32 Arduino has exceptions disabled by default despite the build flag

### Build Verification

After implementation:

```bash
cd firmware/magic
pio run -e heltec_v4
```

Must compile with zero errors. Warnings about unused variables are acceptable during Phase 2.

### Success Criteria

1. `wifi_mx_adapter.h` and `.cpp` exist in `firmware/magic/lib/App/`
2. `WiFiMxAdapter::init()` subscribes to bus
3. `WiFiMxAdapter::drainQueue()` is called from WiFiTask loop
4. `pio run -e heltec_v4` compiles clean
5. All 9 safety checklist items verified
6. No existing files modified except one line added to WiFiTask in `main.cpp`

## PROMPT END
