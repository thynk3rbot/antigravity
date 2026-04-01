# Mx Framework — Oneshot Implementation Prompt

**Date:** 2026-04-01
**Purpose:** Drop this entire document into a fresh Claude Code session to implement the Mx message framework across firmware and daemon.

---

## PROMPT START

You are implementing the **Mx framework** — an internal message bus, last-value cache, and active-object infrastructure for an ESP32-S3 IoT mesh project. This is based on a proven production architecture (deployed at banks, billions of messages/day) adapted for embedded + PC.

### Naming Convention — CRITICAL

**`Mx` prefix for ALL internal framework code.** No exceptions.

- Classes: `MxBus`, `MxQueue`, `MxRecord`, `MxTransport`, `MxConsumer`, `MxMessage`, `MxSystem`
- Files: `mx_bus.h`, `mx_queue.h`, `mx_record.h`, etc.
- Enums: `MxOp`, `MxStatus`
- Namespaces: C++ = no namespace (flat, like existing firmware); Python = `mx/` package

**"Magic" is the EXTERNAL brand only** — device names (`Magic-XXXX`), MQTT topics (`MagicCache/#`), UI labels, user-facing strings. Never use "Magic" as a class name, variable name, or internal identifier. Existing `Magic*` classes in the daemon (`MagicDaemon`, `MagicClient`, `MagicLVCService`, etc.) are legacy application code — do not rename them, do not extend them with Mx patterns. They will be migrated to use Mx later.

**Why:** The codebase already has ~20 `Magic*` identifiers. A search-and-replace on "Magic" would break production. `Mx` has zero collisions.

### What You're Building

#### 1. Message Types (`MxOp` enum)

Seven operations cover every possible action:

```cpp
// firmware/magic/lib/Mx/mx_message.h
enum class MxOp : uint8_t {
    UPDATE = 0,       // field-level merge into existing record
    INSERT,           // new record creation
    REMOVE,           // record deletion
    SUBSCRIBE,        // register interest in a subject
    UNSUBSCRIBE,      // deregister interest
    EXECUTE,          // command execution (STATUS, RELAY, GPIO, etc.)
    WALK              // enumerate all records in a cache
};
```

```python
# daemon/src/mx/mx_message.py
from enum import IntEnum

class MxOp(IntEnum):
    UPDATE = 0
    INSERT = 1
    REMOVE = 2
    SUBSCRIBE = 3
    UNSUBSCRIBE = 4
    EXECUTE = 5
    WALK = 6
```

#### 2. Message Struct (`MxMessage`)

The universal message container. Fixed size per queue type.

```cpp
// firmware/magic/lib/Mx/mx_message.h
struct MxMessage {
    MxOp op;                    // 1 byte — operation type
    uint8_t src_transport;      // 1 byte — which transport sent this (enum)
    uint16_t subject_id;        // 2 bytes — subject identifier (not a string)
    uint8_t payload[252];       // payload — sized to LoRa max (256 - 4 header)
    uint8_t payload_len;        // actual payload length
};
// Total: 257 bytes — fits in one LoRa frame with room for routing header
```

```python
# daemon/src/mx/mx_message.py
from dataclasses import dataclass, field
from typing import Any

@dataclass
class MxMessage:
    op: MxOp
    subject: str                 # PC side uses full string subjects
    payload: dict = field(default_factory=dict)
    src_transport: str = ""
    context: Any = None
```

#### 3. Message Queue (`MxQueue`)

**ESP32:** FreeRTOS static queue carrying 4-byte pointers to pool-allocated messages.
**PC:** `asyncio.Queue` — no constraints, heap is fine.

```cpp
// firmware/magic/lib/Mx/mx_queue.h
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "mx_message.h"
#include "mx_pool.h"

class MxQueue {
public:
    MxQueue(const char* name, uint8_t depth);
    ~MxQueue();

    // Post a message (copies into pool slot, enqueues pointer)
    // Returns false if pool exhausted or queue full
    bool post(const MxMessage& msg);

    // Post from ISR context (RadioLib callback)
    bool postFromISR(const MxMessage& msg, BaseType_t* woken);

    // Blocking receive — waits up to timeout_ms
    // Caller gets a pool slot pointer. MUST call release() when done.
    MxMessage* receive(uint32_t timeout_ms = portMAX_DELAY);

    // Return a message slot to the pool
    void release(MxMessage* msg);

    uint8_t pending() const;
    const char* name() const { return m_name; }

private:
    QueueHandle_t m_queue;
    const char* m_name;
};
```

```python
# daemon/src/mx/mx_queue.py
import asyncio
from .mx_message import MxMessage

class MxQueue:
    def __init__(self, name: str, maxsize: int = 64):
        self.name = name
        self._queue: asyncio.Queue[MxMessage] = asyncio.Queue(maxsize=maxsize)

    async def post(self, msg: MxMessage) -> bool:
        try:
            self._queue.put_nowait(msg)
            return True
        except asyncio.QueueFull:
            return False

    async def receive(self, timeout: float = None) -> MxMessage | None:
        try:
            return await asyncio.wait_for(self._queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None

    def pending(self) -> int:
        return self._queue.qsize()
```

#### 4. Message Pool (`MxPool`) — ESP32 only

Static pre-allocated message slots. No heap allocation ever.

```cpp
// firmware/magic/lib/Mx/mx_pool.h
#pragma once
#include "mx_message.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// One global pool. Size tuned to total system needs.
// 16 slots × 257 bytes = ~4.1KB — fits comfortably in SRAM.
constexpr uint8_t MX_POOL_SIZE = 16;

class MxPool {
public:
    static MxPool& instance();

    // Get a free slot. Returns nullptr if exhausted.
    MxMessage* alloc();

    // ISR-safe alloc (no mutex, uses atomic free-list)
    MxMessage* allocFromISR();

    // Return slot to pool
    void release(MxMessage* slot);

    uint8_t available() const;

private:
    MxPool();
    MxMessage m_slots[MX_POOL_SIZE];
    bool m_free[MX_POOL_SIZE];          // true = available
    SemaphoreHandle_t m_mutex;
    volatile uint8_t m_isr_head;        // atomic scan start for ISR path
};
```

Not needed on PC — Python uses heap. Do NOT create an `mx_pool.py`.

#### 5. Consumer Interface (`MxConsumer`)

Any component that receives messages implements this.

```cpp
// firmware/magic/lib/Mx/mx_consumer.h
#pragma once
#include "mx_message.h"

class MxConsumer {
public:
    virtual ~MxConsumer() = default;

    // Process a message. Called by the owning task's run loop.
    // Returns true if consumed successfully.
    virtual bool consume(const MxMessage& msg) = 0;
};
```

```python
# daemon/src/mx/mx_consumer.py
from abc import ABC, abstractmethod
from .mx_message import MxMessage

class MxConsumer(ABC):
    @abstractmethod
    async def consume(self, msg: MxMessage) -> bool:
        ...
```

#### 6. Last Value Cache (`MxRecord`)

Holds current state per subject. The bandwidth strategy for mesh — only deltas travel over the wire.

```cpp
// firmware/magic/lib/Mx/mx_record.h
#pragma once
#include <cstdint>
#include <cstring>

// Fixed-field record for a known subject.
// Each subject type defines its own struct that embeds MxRecord as header.
struct MxRecord {
    uint16_t subject_id;
    uint32_t sequence;          // monotonic — receivers detect gaps
    uint32_t timestamp_ms;      // millis() at last update
    uint8_t dirty_mask;         // bitmask of changed fields since last publish
};

// Example: node status record
struct MxNodeStatus : public MxRecord {
    uint16_t battery_mv;        // field 0
    int8_t rssi;                // field 1
    float temperature;          // field 2
    uint8_t relay_mask;         // field 3
    uint32_t uptime_s;          // field 4
    float latitude;             // field 5
    float longitude;            // field 6
    float altitude;             // field 7
};
```

```python
# daemon/src/mx/mx_record.py
from dataclasses import dataclass, field
from typing import Any
import time

@dataclass
class MxRecord:
    subject: str
    sequence: int = 0
    timestamp: float = field(default_factory=time.time)
    fields: dict = field(default_factory=dict)
    dirty: set = field(default_factory=set)     # field names changed since last publish

    def update(self, changes: dict):
        """Merge field-level changes. Track dirty fields."""
        for k, v in changes.items():
            if self.fields.get(k) != v:
                self.fields[k] = v
                self.dirty.add(k)
        self.sequence += 1
        self.timestamp = time.time()

    def get_delta(self) -> dict:
        """Return only changed fields, then clear dirty set."""
        delta = {k: self.fields[k] for k in self.dirty if k in self.fields}
        self.dirty.clear()
        return delta

    def snapshot(self) -> dict:
        """Full record — used on subscribe (LVC guarantee)."""
        return dict(self.fields)
```

#### 7. The Bus (`MxBus`)

Routes messages by subject to registered consumers. On firmware, this replaces `CommandManager::route()`. On daemon, this is the central dispatcher.

```cpp
// firmware/magic/lib/Mx/mx_bus.h
#pragma once
#include "mx_message.h"
#include "mx_consumer.h"
#include <array>

constexpr uint8_t MX_MAX_SUBSCRIPTIONS = 32;

struct MxSubscription {
    uint16_t subject_id;        // 0 = unused slot
    MxConsumer* consumer;
    MxQueue* queue;             // target queue (consumer's inbox)
};

class MxBus {
public:
    static MxBus& instance();

    // Subscribe a consumer's queue to a subject
    bool subscribe(uint16_t subject_id, MxConsumer* consumer, MxQueue* queue);
    bool unsubscribe(uint16_t subject_id, MxConsumer* consumer);

    // Publish — copies message to all subscriber queues for this subject
    // Returns number of subscribers reached
    uint8_t publish(const MxMessage& msg);

    // Publish from ISR (RadioLib) — uses postFromISR on target queues
    uint8_t publishFromISR(const MxMessage& msg, BaseType_t* woken);

private:
    MxBus() = default;
    std::array<MxSubscription, MX_MAX_SUBSCRIPTIONS> m_subs{};
};
```

```python
# daemon/src/mx/mx_bus.py
import asyncio
import logging
from collections import defaultdict
from .mx_message import MxMessage, MxOp
from .mx_consumer import MxConsumer
from .mx_queue import MxQueue

log = logging.getLogger("mx.bus")

class MxBus:
    def __init__(self):
        self._subscriptions: dict[str, list[tuple[MxConsumer, MxQueue]]] = defaultdict(list)

    def subscribe(self, subject: str, consumer: MxConsumer, queue: MxQueue):
        self._subscriptions[subject].append((consumer, queue))
        log.info(f"subscribe: {subject} -> {type(consumer).__name__}")

    def unsubscribe(self, subject: str, consumer: MxConsumer):
        self._subscriptions[subject] = [
            (c, q) for c, q in self._subscriptions[subject] if c is not consumer
        ]

    async def publish(self, msg: MxMessage) -> int:
        """Deliver message to all subscriber queues for this subject."""
        subs = self._subscriptions.get(msg.subject, [])
        delivered = 0
        for consumer, queue in subs:
            if await queue.post(msg):
                delivered += 1
            else:
                log.warning(f"queue full: {queue.name} dropped {msg.op.name}")
        return delivered

    def subscriber_count(self, subject: str) -> int:
        return len(self._subscriptions.get(subject, []))
```

#### 8. Transport Interface (`MxTransport`)

Each transport (LoRa, WiFi/HTTP, MQTT, BLE, Serial, ESP-NOW, WebSocket) implements this. This extends — does NOT replace — the existing `TransportInterface` in `firmware/magic/lib/Transport/interface.h`. The existing interface handles wire-level details. `MxTransport` bridges wire ↔ bus.

```cpp
// firmware/magic/lib/Mx/mx_transport.h
#pragma once
#include "mx_message.h"
#include "mx_bus.h"

class MxTransport {
public:
    virtual ~MxTransport() = default;

    // Transport name (for logging/diagnostics)
    virtual const char* name() const = 0;

    // Send a message out over this transport
    // The implementation serializes MxMessage to wire format
    virtual bool send(const MxMessage& msg) = 0;

    // Called when data arrives from the wire
    // The implementation deserializes and posts to the bus
    virtual void onReceive(const uint8_t* data, uint8_t len) = 0;

    // Lifecycle
    virtual bool init() = 0;
    virtual void shutdown() = 0;
};
```

```python
# daemon/src/mx/mx_transport.py
from abc import ABC, abstractmethod
from .mx_message import MxMessage

class MxTransport(ABC):
    @abstractmethod
    def name(self) -> str: ...

    @abstractmethod
    async def send(self, msg: MxMessage) -> bool: ...

    @abstractmethod
    async def start(self): ...

    @abstractmethod
    async def stop(self): ...
```

#### 9. Subject Registry (`MxSubjects`)

Maps 2-byte IDs ↔ string names. Compiled-in on firmware. Dict on daemon.

```cpp
// firmware/magic/lib/Mx/mx_subjects.h
#pragma once
#include <cstdint>

// Subject IDs — both firmware and daemon must agree on these values.
// Add new subjects at the END. Never reorder. Never reuse an ID.
namespace MxSubjects {
    constexpr uint16_t NODE_STATUS    = 0x0001;
    constexpr uint16_t RELAY_STATE    = 0x0002;
    constexpr uint16_t SENSOR_DATA    = 0x0003;
    constexpr uint16_t GPS_POSITION   = 0x0004;
    constexpr uint16_t COMMAND        = 0x0010;
    constexpr uint16_t COMMAND_REPLY  = 0x0011;
    constexpr uint16_t MESH_NEIGHBOR  = 0x0020;
    constexpr uint16_t MESH_ROUTE     = 0x0021;
    constexpr uint16_t OTA_ANNOUNCE   = 0x0030;
    constexpr uint16_t SCHEDULE       = 0x0040;
    constexpr uint16_t HEARTBEAT      = 0x00FF;

    const char* nameOf(uint16_t id);    // for logging only — not for wire
}
```

```python
# daemon/src/mx/mx_subjects.py

# Must match firmware/magic/lib/Mx/mx_subjects.h EXACTLY
SUBJECTS = {
    0x0001: "node_status",
    0x0002: "relay_state",
    0x0003: "sensor_data",
    0x0004: "gps_position",
    0x0010: "command",
    0x0011: "command_reply",
    0x0020: "mesh_neighbor",
    0x0021: "mesh_route",
    0x0030: "ota_announce",
    0x0040: "schedule",
    0x00FF: "heartbeat",
}

BY_NAME = {v: k for k, v in SUBJECTS.items()}
```

### Wire Format — Mesh Protocol

Over LoRa (256 byte max), messages are binary packed:

```
Byte 0:     MxOp (1 byte)
Byte 1:     Source node ID high
Byte 2:     Source node ID low
Byte 3:     Subject ID high
Byte 4:     Subject ID low
Byte 5:     Sequence number (wrapping uint8)
Byte 6:     TTL (hops remaining, max 5)
Byte 7:     Payload length
Byte 8..N:  Payload (binary packed fields)
Byte N+1-2: CRC16
```

Max payload: 256 - 10 (header+CRC) = 246 bytes.

The LVC dirty_mask determines WHICH fields are in the payload. Receiver merges delta into its cached MxRecord. If sequence has a gap, receiver requests full record (WALK operation).

### File Layout

```
firmware/magic/lib/Mx/          ← NEW directory, all new files
├── mx_bus.h / .cpp
├── mx_consumer.h
├── mx_message.h
├── mx_pool.h / .cpp
├── mx_queue.h / .cpp
├── mx_record.h
├── mx_subjects.h / .cpp
├── mx_system.h / .cpp       ← init, state machine, ties it all together
├── mx_transport.h
└── mx_wire.h / .cpp          ← serialize/deserialize for LoRa wire format

daemon/src/mx/                ← NEW directory, all new files
├── __init__.py
├── mx_bus.py
├── mx_consumer.py
├── mx_message.py
├── mx_queue.py
├── mx_record.py
├── mx_subjects.py
├── mx_system.py
└── mx_transport.py
```

### Integration Rules — DO NOT BREAK EXISTING CODE

1. **Do NOT modify any existing file** in Phase 1. The Mx framework lives in its own directory. Existing managers, transports, and command routing continue to work as-is.

2. **Do NOT rename any existing class.** `CommandManager`, `LoRaTransport`, `WiFiTransport`, `StatusBuilder`, `NVSManager` — all stay. Mx wraps them eventually, not replaces them.

3. **Do NOT add Mx includes to existing files.** The framework must compile independently. Test it with a simple main that creates an MxBus, subscribes a dummy consumer, and publishes a message.

4. **Firmware must compile for both V3 and V4.** No `#ifdef` for hardware variant in any Mx file — the framework is hardware-agnostic. Hardware specifics stay in HAL and Transport layers.

5. **Python Mx package must be importable standalone.** `from mx.mx_bus import MxBus` should work without importing the daemon, webapp, or any other module.

### Build Verification

After creating all files:

**Firmware:**
```bash
cd firmware/magic
pio run -e heltec_v4    # must compile clean with Mx/ in lib/
```

**Daemon:**
```python
# daemon/src/test_mx.py — create this as a smoke test
import asyncio
from mx.mx_bus import MxBus
from mx.mx_queue import MxQueue
from mx.mx_message import MxMessage, MxOp
from mx.mx_consumer import MxConsumer
from mx.mx_record import MxRecord

class TestConsumer(MxConsumer):
    def __init__(self):
        self.received = []

    async def consume(self, msg: MxMessage) -> bool:
        self.received.append(msg)
        return True

async def main():
    bus = MxBus()
    consumer = TestConsumer()
    queue = MxQueue("test", maxsize=8)
    bus.subscribe("node_status", consumer, queue)

    msg = MxMessage(op=MxOp.UPDATE, subject="node_status", payload={"battery_mv": 3700})
    delivered = await bus.publish(msg)
    assert delivered == 1

    received = await queue.receive(timeout=1.0)
    assert received is not None
    assert received.op == MxOp.UPDATE

    # LVC test
    rec = MxRecord(subject="node_status")
    rec.update({"battery_mv": 3700, "rssi": -45})
    assert rec.get_delta() == {"battery_mv": 3700, "rssi": -45}
    assert rec.get_delta() == {}    # dirty cleared

    rec.update({"rssi": -50})
    assert rec.get_delta() == {"rssi": -50}

    print("All Mx smoke tests passed")

asyncio.run(main())
```

### What NOT To Do

- Do NOT implement active-object wrappers for existing managers yet. That's Phase 2.
- Do NOT implement the LoRa wire serializer yet. That's Phase 3.
- Do NOT touch `boot_sequence.cpp`, `main.cpp`, or `control_loop.cpp`.
- Do NOT add `MxSystem` initialization to the firmware boot yet.
- Do NOT create documentation files — the code IS the documentation for now.
- Do NOT use `std::string` in any firmware Mx header. Use `const char*` or fixed `char[]`.
- Do NOT use dynamic allocation (`new`, `malloc`, `std::vector`) in any firmware Mx file.
- Do NOT add comments explaining what the code does — it should be self-evident from the naming.

### Success Criteria

1. `firmware/magic/lib/Mx/` directory exists with all `.h` and `.cpp` files listed above
2. `pio run -e heltec_v4` compiles clean (Mx files are in lib/ so PlatformIO picks them up automatically)
3. `daemon/src/mx/` directory exists with all `.py` files listed above
4. `python daemon/src/test_mx.py` passes all assertions
5. No existing file modified
6. No `Magic` used as an identifier anywhere in Mx code
7. No heap allocation in firmware Mx code (`new`, `malloc`, `std::string`, `std::vector`)
8. Subject IDs in Python match firmware exactly

## PROMPT END
