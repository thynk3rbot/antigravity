# Mx Phase 3 — Route Commands Through MxBus

**Date:** 2026-04-01
**Prerequisites:** `git pull origin main` — Phase 2 WiFiMxAdapter is merged
**Goal:** Make CommandManager publish commands to MxBus instead of calling transports directly. Transports receive commands via their MxQueues.

---

## PROMPT START

You are migrating `CommandManager` to publish command responses through the Mx message bus. After this phase, **every command response flows through MxBus** — transports process them asynchronously on their own tasks.

### Background — What Phase 2 Did

Phase 2 created `WiFiMxAdapter` — an active object with an MxQueue that listens for `MX_SUBJ_COMMAND` messages and processes them on WiFiTask. The adapter works, but nothing publishes to it yet. `CommandManager::process()` still calls transports directly via the `ResponseCallback` lambda.

### What Phase 3 Changes

**Before:** Caller → `CommandManager::process(cmd, callback)` → callback fires response string back to caller synchronously
**After:** Caller → `CommandManager::process(cmd, callback)` → same synchronous path PLUS publishes the response to MxBus as `MxOp::UPDATE` on `MX_SUBJ_COMMAND_REPLY`

This is **additive, not replacing**. The existing ResponseCallback still works — it's the transport's immediate response path. The MxBus publish is a broadcast notification that any subscriber (WiFiMxAdapter, future OLED adapter, future MQTT adapter, logging) can listen to.

### What You're Building

**One new file:** `firmware/magic/lib/App/command_mx_bridge.h` (and `.cpp`)

This bridge:
1. Wraps `CommandManager::process()` — calls the original, captures the response
2. After the original callback fires, publishes the response to MxBus as `MxOp::UPDATE` on `MX_SUBJ_COMMAND_REPLY`
3. Also publishes the inbound command as `MxOp::EXECUTE` on `MX_SUBJ_COMMAND` (for any adapter that wants to see all commands)
4. Provides a drop-in replacement function signature that existing callers can switch to

### Architecture

```
Before:
  SerialTransport::poll()  → CommandManager::process(cmd, lambda) → lambda prints response
  HttpAPI::handleCommand() → CommandManager::process(cmd, lambda) → lambda sends HTTP response
  LoRa callback            → CommandManager::process(cmd, lambda) → lambda sends LoRa reply
  (each caller gets response directly, nobody else sees it)

After:
  SerialTransport::poll()  → CommandMxBridge::process(cmd, lambda)
                              ├→ CommandManager::process(cmd, lambda)  ← still works
                              └→ MxBus::publish(COMMAND_REPLY, response)  ← broadcast
                                  ├→ WiFiMxAdapter queue (for HTTP cache update)
                                  ├→ future: OLEDAdapter queue (show last command result)
                                  └→ future: MQTTAdapter queue (telemetry)
```

### Code Structure

```cpp
// firmware/magic/lib/App/command_mx_bridge.h
#pragma once
#include "command_manager.h"
#include "../Mx/mx_bus.h"
#include "../Mx/mx_message.h"
#include "../Mx/mx_subjects.h"

/**
 * CommandMxBridge — Wraps CommandManager to publish commands and responses
 * to MxBus. Drop-in replacement for CommandManager::process() calls.
 */
class CommandMxBridge {
public:
    // Drop-in replacement: same signature as CommandManager::process()
    static void process(const String& input, CommandManager::ResponseCallback callback);
};
```

### Implementation Details

```cpp
// firmware/magic/lib/App/command_mx_bridge.cpp

void CommandMxBridge::process(const String& input, CommandManager::ResponseCallback callback) {

    // 1. Publish inbound command to bus (EXECUTE on COMMAND subject)
    //    Copy up to 246 bytes of command string into MxMessage payload
    MxMessage cmdMsg;
    cmdMsg.op = MxOp::EXECUTE;
    cmdMsg.subject_id = MxSubjects::COMMAND;
    cmdMsg.payload_len = min((size_t)input.length(), (size_t)MX_PAYLOAD_MAX);
    memcpy(cmdMsg.payload, input.c_str(), cmdMsg.payload_len);
    MxBus::instance().publish(cmdMsg);

    // 2. Call original CommandManager — response goes to caller's callback
    String capturedResponse;
    CommandManager::process(input, [&capturedResponse, &callback](const String& response) {
        capturedResponse = response;
        if (callback) callback(response);  // Original callback still fires
    });

    // 3. Publish response to bus (UPDATE on COMMAND_REPLY subject)
    if (capturedResponse.length() > 0) {
        MxMessage replyMsg;
        replyMsg.op = MxOp::UPDATE;
        replyMsg.subject_id = MxSubjects::COMMAND_REPLY;
        replyMsg.payload_len = min((size_t)capturedResponse.length(), (size_t)MX_PAYLOAD_MAX);
        memcpy(replyMsg.payload, capturedResponse.c_str(), replyMsg.payload_len);
        MxBus::instance().publish(replyMsg);
    }
}
```

### Wiring — Change 3 Callers (minimal, mechanical)

These are the ONLY files you modify (beyond creating the new bridge file):

**1. `firmware/magic/src/main.cpp` — Serial CLI (loop function)**
```cpp
// BEFORE (line ~219):
CommandManager::process(input, [](const String& response) {
// AFTER:
#include "../lib/App/command_mx_bridge.h"
CommandMxBridge::process(input, [](const String& response) {
```

**2. `firmware/magic/lib/Transport/serial_transport.cpp` — Serial poll**
```cpp
// BEFORE (line ~46):
CommandManager::process(_rxBuffer, [](const String& response) {
// AFTER:
#include "../App/command_mx_bridge.h"
CommandMxBridge::process(_rxBuffer, [](const String& response) {
```

**3. `firmware/magic/lib/App/http_api.cpp` — HTTP command endpoint**
```cpp
// BEFORE (line ~267):
CommandManager::process(String(cmdBuf), [&responseJson](const String& resp) {
// AFTER:
#include "command_mx_bridge.h"
CommandMxBridge::process(String(cmdBuf), [&responseJson](const String& resp) {
```

**Do NOT change:**
- `boot_sequence.cpp` callers — those run during init before tasks start, MxBus may not have subscribers yet
- `wifi_mx_adapter.cpp` — it calls `CommandManager::process()` directly, which is correct (it's already on the bus side, no need to re-publish)
- `test_command_manager.cpp` — tests exercise CommandManager directly, not the bridge

### MX_PAYLOAD_MAX Reference

Check `mx_message.h` for the payload size constant. It should be 246 bytes (257 total - 11 byte header). If the constant name differs, use whatever is defined there.

```cpp
// Expected in mx_message.h:
constexpr size_t MX_PAYLOAD_MAX = 246;
```

If this constant doesn't exist, define it in `command_mx_bridge.cpp` locally:
```cpp
static constexpr size_t MX_PAYLOAD_MAX = sizeof(MxMessage::payload);
```

### ESP32/FreeRTOS Safety Checklist — MANDATORY

- [ ] **`CommandMxBridge::process()` runs on the CALLER's task** — it is NOT an active object. It runs synchronously: publish command → call CommandManager → publish reply → return. This is safe because the caller already owns its execution context.
- [ ] **`MxBus::publish()` posts to subscriber queues non-blocking** — if a queue is full, the message is dropped (not blocked). This is correct behavior: callers must never block.
- [ ] **`capturedResponse` is a stack-local String** — lives only for the duration of `process()`. The `memcpy` into `replyMsg.payload` copies the bytes before the String is destroyed. This is correct.
- [ ] **No new FreeRTOS tasks, no new queues** — the bridge is pure logic, no threading primitives
- [ ] **`min()` prevents buffer overflow** — payload is capped at `MX_PAYLOAD_MAX` bytes
- [ ] **`boot_sequence.cpp` callers are NOT modified** — they run before MxBus has subscribers
- [ ] **`wifi_mx_adapter.cpp` is NOT modified** — it already uses CommandManager directly (correct)
- [ ] **Include paths are correct** — verify relative `../Mx/` and `../App/` paths compile

### What NOT To Do

1. **Do NOT modify `CommandManager` itself** — the bridge wraps it, doesn't change it
2. **Do NOT modify any Mx framework files** in `lib/Mx/`
3. **Do NOT modify `wifi_mx_adapter.cpp`** — it's already correct
4. **Do NOT modify `boot_sequence.cpp` callers** — init-time commands don't need bus broadcast
5. **Do NOT add async behavior** — the bridge is synchronous and that's correct for Phase 3
6. **Do NOT truncate response silently** — if response exceeds 246 bytes, the truncated version is still useful (JSON may be incomplete, but that's a Phase 4 concern: chunked responses)

### Build Verification

```bash
cd firmware/magic
pio run -e heltec_v4
```

Must compile with zero errors.

### Success Criteria

1. `command_mx_bridge.h` and `.cpp` exist in `firmware/magic/lib/App/`
2. 3 callers switched from `CommandManager::process` to `CommandMxBridge::process`
3. `boot_sequence.cpp` and `wifi_mx_adapter.cpp` callers are unchanged
4. `pio run -e heltec_v4` compiles clean
5. All 8 safety checklist items verified
6. Existing behavior is preserved — Serial CLI, HTTP API, and LoRa commands still work exactly as before

## PROMPT END
