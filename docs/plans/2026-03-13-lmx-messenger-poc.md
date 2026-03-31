# LMX Messenger Proof-of-Concept Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** End-to-end text messaging over LoRa mesh — PC daemon sends a message via BLE/Serial/WiFi to a Magic device, which transmits over LoRa to another device, which delivers to a second PC daemon. ACK confirmation on delivery.

**Architecture:** New `MsgManager` firmware singleton handles LMX packet encode/decode/dedup/ACK. New `tools/loramsg/` Python daemon with SQLite message store, transport bridge, and WebSocket-served PWA chat UI. LMX packets use the existing AES-GCM encryption and are transport-agnostic (same packet over LoRa, BLE, Serial, WiFi).

**Tech Stack:** C++ (ESP32-S3 PlatformIO), Python 3.10+ (FastAPI, bleak, pyserial, aiohttp, pystray), SQLite, vanilla JS PWA.

**PoC Scope (what ships):**
- Firmware: MsgManager with LMX encode/decode, dedup, ACK, managed flooding
- PC daemon: transport bridge (Serial + HTTP), message store, WebSocket API
- PWA: minimal chat UI (send/receive/delivery status)
- NOT in PoC: store-and-forward, internet bridge, system tray, BLE transport in daemon

---

## Task 1: LMX Protocol Header — Shared Constants

**Files:**
- Create: `src/managers/MsgManager.h`

**Step 1: Create MsgManager header with LMX protocol constants and class skeleton**

```cpp
#pragma once

#include <Arduino.h>
#include <functional>
#include "config.h"

// ── LMX Protocol Constants ──────────────────────────────────────
#define LMX_SYNC_0       0xAA
#define LMX_SYNC_1       0x4D
#define LMX_HEADER_SIZE  12
#define LMX_MAX_PAYLOAD  225
#define LMX_MAX_PACKET   (LMX_HEADER_SIZE + LMX_MAX_PAYLOAD)
#define LMX_BROADCAST    0xFF   // Broadcast dest (1-byte short ID)
#define LMX_DEDUP_SIZE   64
#define LMX_DEDUP_TTL_MS 300000 // 5 minutes
#define LMX_MAX_RETRIES  3
#define LMX_ACK_TIMEOUT_BASE_MS 5000

enum class LmxMsgType : uint8_t {
  TEXT          = 0x0,
  ACK           = 0x1,
  NACK          = 0x2,
  SAF_OFFER     = 0x3,
  SAF_REQUEST   = 0x4,
  SAF_DELIVERY  = 0x5,
  NODE_ANNOUNCE = 0x6,
  PING          = 0x7,
  FRAGMENT      = 0x8,
  AUDIO         = 0x9,
  IMAGE         = 0xA,
};

// ── LMX Packet Header (12 bytes, cleartext for routing) ─────────
struct __attribute__((packed)) LmxHeader {
  uint8_t  sync[2];      // {0xAA, 0x4D}
  uint8_t  dest;          // Destination short ID (0xFF=broadcast)
  uint8_t  src;           // Original sender short ID
  uint32_t packetId;      // Unique message ID (monotonic counter)
  uint8_t  flags;         // [HopLimit:3][WantAck:1][MsgType:4]
  uint8_t  hopStart;      // Original hop limit

  // Helpers
  uint8_t  hopLimit() const { return (flags >> 5) & 0x07; }
  bool     wantAck()  const { return (flags >> 4) & 0x01; }
  LmxMsgType msgType() const { return (LmxMsgType)(flags & 0x0F); }

  void setFlags(uint8_t hops, bool ack, LmxMsgType type) {
    flags = ((hops & 0x07) << 5) | ((ack ? 1 : 0) << 4) | ((uint8_t)type & 0x0F);
  }
};

// ── Dedup Entry ─────────────────────────────────────────────────
struct LmxDedupEntry {
  uint8_t  src;
  uint32_t packetId;
  unsigned long seenMs;
};

// ── Pending ACK for reliable delivery ───────────────────────────
struct LmxPendingAck {
  bool     active;
  uint8_t  dest;
  uint32_t packetId;
  uint8_t  packet[LMX_MAX_PACKET + 28]; // + GCM overhead
  size_t   packetLen;
  int      retryCount;
  unsigned long lastAttemptMs;
  unsigned long timeoutMs;
};

// ── MsgManager Singleton ────────────────────────────────────────
class MsgManager {
public:
  static MsgManager &getInstance() {
    static MsgManager instance;
    return instance;
  }

  void init();

  // Build and send a text message to dest (short ID)
  bool sendText(uint8_t dest, const String &text, bool wantAck = true);

  // Process a raw decrypted LMX packet from any interface
  void handleLmxPacket(const uint8_t *data, size_t len, CommInterface source);

  // Tick — call from ScheduleManager task (50ms)
  void tick();

  // Command handler for "MSG ..." commands
  void handleMsgCommand(const String &args, CommInterface source);

  // Stats
  uint32_t getTxCount() const { return _txCount; }
  uint32_t getRxCount() const { return _rxCount; }

  // Callback for incoming text messages (daemon reads these)
  typedef std::function<void(uint8_t src, const String &text, int hopsUsed)> MsgCallback;
  void setOnMessage(MsgCallback cb) { _onMessage = cb; }

private:
  MsgManager();

  // Encode LMX header + payload, encrypt, and transmit via LoRa
  bool _sendLmxPacket(uint8_t dest, LmxMsgType type, bool wantAck,
                      const uint8_t *payload, size_t payloadLen);

  // Send ACK for a received packet
  void _sendAck(uint8_t dest, uint32_t originalPacketId);

  // Dedup
  bool _isDuplicate(uint8_t src, uint32_t packetId);
  void _markSeen(uint8_t src, uint32_t packetId);

  // Rebroadcast (managed flooding)
  void _rebroadcast(const uint8_t *rawPacket, size_t len);

  // Retry engine
  void _checkRetries();
  static const int MAX_PENDING = 5;
  LmxPendingAck _pending[MAX_PENDING];

  // Dedup cache
  LmxDedupEntry _dedup[LMX_DEDUP_SIZE];
  int _dedupHead = 0;

  // Counters
  uint32_t _nextPacketId = 1;
  uint32_t _txCount = 0;
  uint32_t _rxCount = 0;

  MsgCallback _onMessage = nullptr;
};
```

**Step 2: Verify it compiles**

Run: `pio run -e heltec_wifi_lora_32_V3`
Expected: Compiles (header only, not yet included anywhere)

**Step 3: Commit**

```bash
git add src/managers/MsgManager.h
git commit -m "feat(lmx): add MsgManager header with LMX protocol structs"
```

---

## Task 2: MsgManager Core — Encode, Decode, Dedup

**Files:**
- Create: `src/managers/MsgManager.cpp`
- Modify: `src/managers/MsgManager.h` (if needed)

**Step 1: Implement MsgManager core**

```cpp
#include "MsgManager.h"
#include "LoRaManager.h"
#include "DataManager.h"
#include "CommandManager.h"
#include "../crypto.h"

MsgManager::MsgManager() {
  memset(_dedup, 0, sizeof(_dedup));
  memset(_pending, 0, sizeof(_pending));
}

void MsgManager::init() {
  _nextPacketId = (uint32_t)(esp_random() & 0xFFFF); // Random start to avoid collisions
  LOG_PRINTLN("MSG: MsgManager initialized");

  // Register MSG command with CommandManager
  CommandManager::getInstance().registerCommand("MSG",
    [](const String &args, CommInterface source) {
      MsgManager::getInstance().handleMsgCommand(args, source);
    });
}

// ── Dedup ────────────────────────────────────────────────────────

bool MsgManager::_isDuplicate(uint8_t src, uint32_t packetId) {
  unsigned long now = millis();
  for (int i = 0; i < LMX_DEDUP_SIZE; i++) {
    if (_dedup[i].src == src && _dedup[i].packetId == packetId &&
        (now - _dedup[i].seenMs) < LMX_DEDUP_TTL_MS) {
      return true;
    }
  }
  return false;
}

void MsgManager::_markSeen(uint8_t src, uint32_t packetId) {
  _dedup[_dedupHead] = {src, packetId, millis()};
  _dedupHead = (_dedupHead + 1) % LMX_DEDUP_SIZE;
}

// ── Build & Send ────────────────────────────────────────────────

bool MsgManager::_sendLmxPacket(uint8_t dest, LmxMsgType type, bool wantAck,
                                const uint8_t *payload, size_t payloadLen) {
  if (payloadLen > LMX_MAX_PAYLOAD) return false;

  uint8_t raw[LMX_MAX_PACKET];
  LmxHeader *hdr = (LmxHeader *)raw;

  hdr->sync[0] = LMX_SYNC_0;
  hdr->sync[1] = LMX_SYNC_1;
  hdr->dest = dest;
  hdr->src = DataManager::getInstance().getMyShortId();
  hdr->packetId = _nextPacketId++;
  hdr->setFlags(3, wantAck, type); // 3 hops default
  hdr->hopStart = 3;

  if (payloadLen > 0) {
    memcpy(raw + LMX_HEADER_SIZE, payload, payloadLen);
  }

  size_t totalRaw = LMX_HEADER_SIZE + payloadLen;

  // Encrypt only the payload portion (header stays cleartext for routing)
  // Wire: [Header 12B cleartext] [IV 12B] [Tag 16B] [encrypted payload]
  uint8_t txBuf[LMX_MAX_PACKET + 28]; // 28 = IV + Tag
  memcpy(txBuf, raw, LMX_HEADER_SIZE); // Copy header as-is

  if (payloadLen > 0) {
    encryptData(raw + LMX_HEADER_SIZE, payloadLen,
                txBuf + LMX_HEADER_SIZE,
                LoRaManager::getInstance().currentKey);
  }

  size_t txLen = LMX_HEADER_SIZE + (payloadLen > 0 ? payloadLen + 28 : 0);

  // Mark our own packet as seen (prevent echo)
  _markSeen(hdr->src, hdr->packetId);

  // Queue for ACK tracking if needed
  if (wantAck && dest != LMX_BROADCAST) {
    for (int i = 0; i < MAX_PENDING; i++) {
      if (!_pending[i].active) {
        _pending[i].active = true;
        _pending[i].dest = dest;
        _pending[i].packetId = hdr->packetId;
        memcpy(_pending[i].packet, txBuf, txLen);
        _pending[i].packetLen = txLen;
        _pending[i].retryCount = 0;
        _pending[i].lastAttemptMs = millis();
        _pending[i].timeoutMs = LMX_ACK_TIMEOUT_BASE_MS;
        break;
      }
    }
  }

  LoRaManager::getInstance().SendRawLoRa(txBuf, txLen);
  _txCount++;
  LOG_PRINTF("LMX TX: dest=%02X type=%d len=%u pktId=%u\n",
             dest, (int)type, (unsigned)txLen, hdr->packetId);
  return true;
}

bool MsgManager::sendText(uint8_t dest, const String &text, bool wantAck) {
  return _sendLmxPacket(dest, LmxMsgType::TEXT, wantAck,
                        (const uint8_t *)text.c_str(), text.length());
}

void MsgManager::_sendAck(uint8_t dest, uint32_t originalPacketId) {
  uint8_t payload[4];
  memcpy(payload, &originalPacketId, 4);
  _sendLmxPacket(dest, LmxMsgType::ACK, false, payload, 4);
}

// ── Receive & Route ─────────────────────────────────────────────

void MsgManager::handleLmxPacket(const uint8_t *data, size_t len,
                                 CommInterface source) {
  if (len < LMX_HEADER_SIZE) return;

  const LmxHeader *hdr = (const LmxHeader *)data;

  // Validate sync
  if (hdr->sync[0] != LMX_SYNC_0 || hdr->sync[1] != LMX_SYNC_1) return;

  // Dedup check
  if (_isDuplicate(hdr->src, hdr->packetId)) {
    LOG_PRINTLN("LMX RX: duplicate, dropping");
    return;
  }
  _markSeen(hdr->src, hdr->packetId);
  _rxCount++;

  uint8_t myId = DataManager::getInstance().getMyShortId();
  bool isForMe = (hdr->dest == myId || hdr->dest == LMX_BROADCAST);

  // Decrypt payload if present
  size_t encLen = len - LMX_HEADER_SIZE;
  uint8_t plainPayload[LMX_MAX_PAYLOAD];
  size_t payloadLen = 0;

  if (encLen > 28) { // IV(12) + Tag(16) minimum
    payloadLen = encLen - 28;
    if (!decryptData(data + LMX_HEADER_SIZE, payloadLen, plainPayload,
                     LoRaManager::getInstance().currentKey)) {
      LOG_PRINTLN("LMX RX: decrypt failed");
      return;
    }
  }

  LmxMsgType type = hdr->msgType();

  // ── Handle ACK ──
  if (type == LmxMsgType::ACK && payloadLen >= 4) {
    uint32_t ackedId;
    memcpy(&ackedId, plainPayload, 4);
    for (int i = 0; i < MAX_PENDING; i++) {
      if (_pending[i].active && _pending[i].packetId == ackedId) {
        _pending[i].active = false;
        LOG_PRINTF("LMX ACK: confirmed pktId=%u\n", ackedId);
        break;
      }
    }
    // Don't rebroadcast ACKs for now (direct return)
    return;
  }

  // ── Handle TEXT ──
  if (isForMe && type == LmxMsgType::TEXT && payloadLen > 0) {
    String text;
    text.concat((const char *)plainPayload, payloadLen);
    int hopsUsed = hdr->hopStart - hdr->hopLimit();
    LOG_PRINTF("LMX RX MSG from %02X: \"%s\" (hops=%d)\n",
               hdr->src, text.c_str(), hopsUsed);

    // Send ACK if requested
    if (hdr->wantAck() && hdr->dest != LMX_BROADCAST) {
      _sendAck(hdr->src, hdr->packetId);
    }

    // Notify callback (daemon reads this)
    if (_onMessage) {
      _onMessage(hdr->src, text, hopsUsed);
    }

    // Also show on Serial for debugging
    LoRaManager::getInstance().lastMsgReceived =
        String("LMX:") + String(hdr->src, HEX) + ": " + text;
  }

  // ── Managed Flooding: Rebroadcast if hop limit allows ──
  if (hdr->hopLimit() > 0 && !isForMe) {
    _rebroadcast(data, len);
  }
  // Also rebroadcast broadcasts even if we consumed them
  if (hdr->hopLimit() > 0 && hdr->dest == LMX_BROADCAST) {
    _rebroadcast(data, len);
  }
}

void MsgManager::_rebroadcast(const uint8_t *rawPacket, size_t len) {
  // Decrement hop limit in a copy
  uint8_t copy[LMX_MAX_PACKET + 28];
  memcpy(copy, rawPacket, len);
  LmxHeader *hdr = (LmxHeader *)copy;

  uint8_t currentHops = hdr->hopLimit();
  if (currentHops == 0) return;

  // Rebuild flags with decremented hop
  hdr->setFlags(currentHops - 1, hdr->wantAck(), hdr->msgType());

  // Random jitter 100-500ms to reduce collision probability
  delay(random(100, 500));

  LoRaManager::getInstance().SendRawLoRa(copy, len);
  LOG_PRINTF("LMX RPTR: rebroadcast pktId=%u hops=%d→%d\n",
             hdr->packetId, currentHops, currentHops - 1);
}

// ── Retry Engine ────────────────────────────────────────────────

void MsgManager::_checkRetries() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_PENDING; i++) {
    if (!_pending[i].active) continue;
    if ((now - _pending[i].lastAttemptMs) < _pending[i].timeoutMs) continue;

    if (_pending[i].retryCount >= LMX_MAX_RETRIES) {
      LOG_PRINTF("LMX FAIL: pktId=%u exhausted %d retries\n",
                 _pending[i].packetId, LMX_MAX_RETRIES);
      _pending[i].active = false;
      continue;
    }

    _pending[i].retryCount++;
    _pending[i].lastAttemptMs = now;
    _pending[i].timeoutMs *= 2; // Exponential backoff

    LoRaManager::getInstance().SendRawLoRa(
        _pending[i].packet, _pending[i].packetLen);
    LOG_PRINTF("LMX RETRY: pktId=%u attempt=%d\n",
               _pending[i].packetId, _pending[i].retryCount);
  }
}

void MsgManager::tick() {
  _checkRetries();
}

// ── Command Handler: MSG SEND <dest_hex> <text> ─────────────────

void MsgManager::handleMsgCommand(const String &args, CommInterface source) {
  String trimmed = args;
  trimmed.trim();

  if (trimmed.startsWith("SEND ") || trimmed.startsWith("send ")) {
    String rest = trimmed.substring(5);
    rest.trim();
    int spaceIdx = rest.indexOf(' ');
    if (spaceIdx < 1) {
      CommandManager::getInstance().sendResponse(
          "MSG: Usage: MSG SEND <destHex> <text>", source);
      return;
    }
    String destStr = rest.substring(0, spaceIdx);
    String text = rest.substring(spaceIdx + 1);

    uint8_t dest = (uint8_t)strtoul(destStr.c_str(), nullptr, 16);
    bool ok = sendText(dest, text, true);
    CommandManager::getInstance().sendResponse(
        ok ? "MSG: queued for " + destStr : "MSG: send failed", source);

  } else if (trimmed.equalsIgnoreCase("STATUS")) {
    String status = "MSG: tx=" + String(_txCount) + " rx=" + String(_rxCount) +
                    " pending=";
    int pendCount = 0;
    for (int i = 0; i < MAX_PENDING; i++) {
      if (_pending[i].active) pendCount++;
    }
    status += String(pendCount);
    CommandManager::getInstance().sendResponse(status, source);

  } else {
    CommandManager::getInstance().sendResponse(
        "MSG: Commands: SEND <dest> <text> | STATUS", source);
  }
}
```

**Step 2: Verify it compiles (not yet wired into main)**

Run: `pio run -e heltec_wifi_lora_32_V3`
Expected: Compiles clean

**Step 3: Commit**

```bash
git add src/managers/MsgManager.cpp
git commit -m "feat(lmx): implement MsgManager core — encode, decode, dedup, ACK, retry"
```

---

## Task 3: Wire MsgManager into Firmware

**Files:**
- Modify: `src/main.cpp` — add MsgManager::init() to setup()
- Modify: `src/managers/ScheduleManager.cpp` — add tick() task
- Modify: `src/managers/LoRaManager.cpp` — intercept LMX packets in ProcessPacket()

**Step 1: Add MsgManager init to main.cpp setup()**

In `src/main.cpp`, after `ScheduleManager::getInstance().init();` add:

```cpp
#include "managers/MsgManager.h"

// In setup(), after ScheduleManager init:
MsgManager::getInstance().init();
```

**Step 2: Add MsgManager tick to ScheduleManager**

In `src/managers/ScheduleManager.cpp`, add a 200ms task for MsgManager:

Add to the task list:
```cpp
#include "MsgManager.h"

// In the task init section, add a new task:
// tMsg(200, TASK_FOREVER, &msgTask)
// Enable it alongside the other tasks
```

Static callback:
```cpp
void ScheduleManager::msgTask() {
  MsgManager::getInstance().tick();
}
```

Add to `ScheduleManager.h`:
```cpp
static void msgTask();
```

**Step 3: Intercept LMX packets in LoRaManager::ProcessPacket()**

In `LoRaManager.cpp`, inside `ProcessPacket()`, after successful decryption and before the existing binary/text checks, add LMX detection:

```cpp
// Check for LMX packet (sync bytes 0xAA 0x4D)
// LMX packets arrive encrypted as a whole unit — header + payload
// But we need the header in cleartext for routing, so LMX uses a
// different encryption scheme: only payload is encrypted, header is clear.
// For LoRa transport, the entire LMX packet (header+encrypted_payload)
// is sent as the LoRa frame content WITHOUT the standard LoRa encryption.

// Actually — for the PoC, detect LMX at the RAW (pre-decrypt) level:
// LMX frames start with 0xAA 0x4D. The existing LoRa path wraps
// everything in GCM, so we need a NEW entry point.
```

**IMPORTANT DESIGN DECISION:** For the PoC, LMX packets will be sent/received via `SendRawLoRa` which bypasses the standard MessagePacket encryption. The LMX packet itself handles its own encryption (header cleartext, payload encrypted). To detect them on RX, we check the first 2 bytes of the encrypted buffer BEFORE decryption — if they match `0xAA 0x4D`, route to MsgManager instead of the normal decrypt path.

In `ProcessPacket()`, add early intercept:

```cpp
void LoRaManager::ProcessPacket(uint8_t *rxEncBuf, int size) {
  // ── LMX Packet Detection (header is cleartext) ──
  if (size >= LMX_HEADER_SIZE && rxEncBuf[0] == 0xAA && rxEncBuf[1] == 0x4D) {
    MsgManager::getInstance().handleLmxPacket(rxEncBuf, size, CommInterface::COMM_LORA);
    _enterRx();
    return;
  }

  // ... existing GCM decrypt path continues below
```

Add `#include "MsgManager.h"` at the top of LoRaManager.cpp.

**Step 4: Build and verify**

Run: `pio run -e heltec_wifi_lora_32_V3`
Expected: Clean compile

**Step 5: Commit**

```bash
git add src/main.cpp src/managers/ScheduleManager.cpp src/managers/ScheduleManager.h src/managers/LoRaManager.cpp
git commit -m "feat(lmx): wire MsgManager into firmware boot, task loop, and LoRa RX"
```

---

## Task 4: PC Daemon — Project Structure and Transport Bridge

**Files:**
- Create: `tools/loramsg/daemon.py`
- Create: `tools/loramsg/lmx_protocol.py`
- Create: `tools/loramsg/transport_bridge.py`
- Create: `tools/loramsg/requirements.txt`

**Step 1: Create LMX protocol codec (Python mirror of firmware)**

```python
# tools/loramsg/lmx_protocol.py
"""LMX protocol encoder/decoder — mirrors firmware MsgManager."""

import struct
from enum import IntEnum
from dataclasses import dataclass

class LmxMsgType(IntEnum):
    TEXT          = 0x0
    ACK           = 0x1
    NACK          = 0x2
    SAF_OFFER     = 0x3
    SAF_REQUEST   = 0x4
    SAF_DELIVERY  = 0x5
    NODE_ANNOUNCE = 0x6
    PING          = 0x7
    FRAGMENT      = 0x8
    AUDIO         = 0x9
    IMAGE         = 0xA

LMX_SYNC = bytes([0xAA, 0x4D])
LMX_HEADER_SIZE = 12
LMX_BROADCAST = 0xFF

@dataclass
class LmxPacket:
    dest: int
    src: int
    packet_id: int
    hop_limit: int
    want_ack: bool
    msg_type: LmxMsgType
    hop_start: int
    payload: bytes

    @property
    def hops_used(self) -> int:
        return self.hop_start - self.hop_limit

    def encode_header(self) -> bytes:
        flags = ((self.hop_limit & 0x07) << 5) | \
                ((1 if self.want_ack else 0) << 4) | \
                (int(self.msg_type) & 0x0F)
        return struct.pack('<2sBBIBB',
                           LMX_SYNC, self.dest, self.src,
                           self.packet_id, flags, self.hop_start)

    @classmethod
    def decode(cls, data: bytes) -> 'LmxPacket | None':
        if len(data) < LMX_HEADER_SIZE:
            return None
        if data[0:2] != LMX_SYNC:
            return None
        _, dest, src, pkt_id, flags, hop_start = struct.unpack_from(
            '<2sBBIBB', data, 0)
        hop_limit = (flags >> 5) & 0x07
        want_ack = bool((flags >> 4) & 0x01)
        msg_type = LmxMsgType(flags & 0x0F)
        payload = data[LMX_HEADER_SIZE:]
        return cls(dest, src, pkt_id, hop_limit, want_ack,
                   msg_type, hop_start, payload)

def make_text_packet(dest: int, src: int, packet_id: int,
                     text: str, want_ack: bool = True) -> LmxPacket:
    return LmxPacket(
        dest=dest, src=src, packet_id=packet_id,
        hop_limit=3, want_ack=want_ack,
        msg_type=LmxMsgType.TEXT, hop_start=3,
        payload=text.encode('utf-8'))
```

**Step 2: Create transport bridge (Serial + HTTP for PoC)**

```python
# tools/loramsg/transport_bridge.py
"""Transport bridge — sends MSG commands to Magic device via Serial or HTTP."""

import asyncio
import logging
import serial_asyncio
import aiohttp

log = logging.getLogger("loramsg.transport")

class SerialTransport:
    """Async serial connection to Magic device."""

    def __init__(self, port: str = "COM3", baud: int = 115200):
        self.port = port
        self.baud = baud
        self._reader = None
        self._writer = None
        self._connected = False
        self._rx_callbacks = []

    async def connect(self):
        try:
            self._reader, self._writer = await serial_asyncio.open_serial_connection(
                url=self.port, baudrate=self.baud)
            self._connected = True
            log.info(f"Serial connected: {self.port}")
            asyncio.create_task(self._read_loop())
        except Exception as e:
            log.error(f"Serial connect failed: {e}")
            self._connected = False

    async def _read_loop(self):
        while self._connected:
            try:
                line = await self._reader.readline()
                text = line.decode('utf-8', errors='replace').strip()
                if text:
                    for cb in self._rx_callbacks:
                        cb(text)
            except Exception as e:
                log.error(f"Serial read error: {e}")
                self._connected = False
                break

    def on_receive(self, callback):
        self._rx_callbacks.append(callback)

    async def send_command(self, cmd: str) -> bool:
        if not self._connected:
            return False
        try:
            self._writer.write((cmd + "\n").encode('utf-8'))
            await self._writer.drain()
            return True
        except Exception as e:
            log.error(f"Serial write error: {e}")
            return False

    @property
    def is_connected(self) -> bool:
        return self._connected


class HttpTransport:
    """HTTP transport to Magic device (WiFi)."""

    def __init__(self, host: str = "192.168.4.1"):
        self.host = host
        self._session = None

    async def connect(self):
        self._session = aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=5))
        log.info(f"HTTP transport ready: {self.host}")

    async def send_command(self, cmd: str) -> str | None:
        if not self._session:
            return None
        try:
            async with self._session.post(
                f"http://{self.host}/api/cmd",
                data={"cmd": cmd}) as resp:
                return await resp.text()
        except Exception as e:
            log.error(f"HTTP error: {e}")
            return None

    async def get_status(self) -> dict | None:
        if not self._session:
            return None
        try:
            async with self._session.get(
                f"http://{self.host}/api/status") as resp:
                return await resp.json()
        except Exception:
            return None

    async def close(self):
        if self._session:
            await self._session.close()


class TransportBridge:
    """Auto-negotiating transport: tries Serial first, then HTTP."""

    def __init__(self, serial_port: str | None = None,
                 http_host: str | None = None):
        self.serial = SerialTransport(serial_port) if serial_port else None
        self.http = HttpTransport(http_host) if http_host else None
        self._rx_callbacks = []

    async def connect(self):
        if self.serial:
            await self.serial.connect()
            self.serial.on_receive(self._on_serial_rx)
        if self.http:
            await self.http.connect()

    def _on_serial_rx(self, line: str):
        for cb in self._rx_callbacks:
            cb(line)

    def on_receive(self, callback):
        self._rx_callbacks.append(callback)

    async def send_msg(self, dest_hex: str, text: str) -> bool:
        cmd = f"MSG SEND {dest_hex} {text}"
        if self.serial and self.serial.is_connected:
            return await self.serial.send_command(cmd)
        if self.http:
            result = await self.http.send_command(cmd)
            return result is not None
        return False

    async def send_raw(self, cmd: str) -> bool:
        if self.serial and self.serial.is_connected:
            return await self.serial.send_command(cmd)
        if self.http:
            result = await self.http.send_command(cmd)
            return result is not None
        return False
```

**Step 3: Create requirements.txt**

```
fastapi>=0.104.0
uvicorn>=0.24.0
aiohttp>=3.9.0
pyserial-asyncio>=0.6
websockets>=12.0
```

**Step 4: Commit**

```bash
git add tools/loramsg/lmx_protocol.py tools/loramsg/transport_bridge.py tools/loramsg/requirements.txt
git commit -m "feat(loramsg): add LMX protocol codec and transport bridge"
```

---

## Task 5: PC Daemon — Message Store and WebSocket API

**Files:**
- Create: `tools/loramsg/message_store.py`
- Create: `tools/loramsg/daemon.py`

**Step 1: Create SQLite message store**

```python
# tools/loramsg/message_store.py
"""SQLite message store with delivery tracking."""

import sqlite3
import time
import json
from dataclasses import dataclass, asdict
from enum import Enum
from pathlib import Path

class MessageStatus(str, Enum):
    QUEUED    = "queued"
    SENT      = "sent"
    DELIVERED = "delivered"
    READ      = "read"
    FAILED    = "failed"

@dataclass
class Message:
    id: int
    dest: int
    src: int
    text: str
    status: str
    packet_id: int
    timestamp: float
    hops: int = 0

    def to_dict(self):
        d = asdict(self)
        d['dest_hex'] = f"{self.dest:02X}"
        d['src_hex'] = f"{self.src:02X}"
        return d

@dataclass
class Contact:
    node_id: int
    alias: str
    last_seen: float
    hops: int = 0

    def to_dict(self):
        d = asdict(self)
        d['node_hex'] = f"{self.node_id:02X}"
        return d


class MessageStore:
    def __init__(self, db_path: str = "loramsg.db"):
        self.db_path = db_path
        self._conn = sqlite3.connect(db_path)
        self._conn.row_factory = sqlite3.Row
        self._create_tables()

    def _create_tables(self):
        self._conn.executescript("""
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                dest INTEGER NOT NULL,
                src INTEGER NOT NULL,
                text TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'queued',
                packet_id INTEGER,
                timestamp REAL NOT NULL,
                hops INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS contacts (
                node_id INTEGER PRIMARY KEY,
                alias TEXT NOT NULL,
                last_seen REAL NOT NULL,
                hops INTEGER DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_msg_dest ON messages(dest);
            CREATE INDEX IF NOT EXISTS idx_msg_status ON messages(status);
        """)
        self._conn.commit()

    def add_outgoing(self, dest: int, src: int, text: str,
                     packet_id: int) -> Message:
        cur = self._conn.execute(
            "INSERT INTO messages (dest, src, text, status, packet_id, timestamp) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (dest, src, text, MessageStatus.SENT.value, packet_id, time.time()))
        self._conn.commit()
        return Message(cur.lastrowid, dest, src, text,
                       MessageStatus.SENT.value, packet_id, time.time())

    def add_incoming(self, src: int, dest: int, text: str,
                     packet_id: int, hops: int) -> Message:
        cur = self._conn.execute(
            "INSERT INTO messages (dest, src, text, status, packet_id, timestamp, hops) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            (dest, src, text, MessageStatus.DELIVERED.value,
             packet_id, time.time(), hops))
        self._conn.commit()
        return Message(cur.lastrowid, dest, src, text,
                       MessageStatus.DELIVERED.value, packet_id, time.time(), hops)

    def update_status(self, packet_id: int, status: MessageStatus):
        self._conn.execute(
            "UPDATE messages SET status = ? WHERE packet_id = ?",
            (status.value, packet_id))
        self._conn.commit()

    def get_conversation(self, node_id: int, limit: int = 50) -> list[Message]:
        rows = self._conn.execute(
            "SELECT * FROM messages WHERE dest = ? OR src = ? "
            "ORDER BY timestamp DESC LIMIT ?",
            (node_id, node_id, limit)).fetchall()
        return [Message(**dict(r)) for r in rows]

    def get_recent(self, limit: int = 100) -> list[Message]:
        rows = self._conn.execute(
            "SELECT * FROM messages ORDER BY timestamp DESC LIMIT ?",
            (limit,)).fetchall()
        return [Message(**dict(r)) for r in rows]

    def upsert_contact(self, node_id: int, alias: str | None = None,
                       hops: int = 0):
        existing = self._conn.execute(
            "SELECT * FROM contacts WHERE node_id = ?",
            (node_id,)).fetchone()
        if existing:
            self._conn.execute(
                "UPDATE contacts SET last_seen = ?, hops = ? WHERE node_id = ?",
                (time.time(), hops, node_id))
        else:
            if not alias:
                alias = f"Node-{node_id:02X}"
            self._conn.execute(
                "INSERT INTO contacts (node_id, alias, last_seen, hops) "
                "VALUES (?, ?, ?, ?)",
                (node_id, alias, time.time(), hops))
        self._conn.commit()

    def get_contacts(self) -> list[Contact]:
        rows = self._conn.execute(
            "SELECT * FROM contacts ORDER BY last_seen DESC").fetchall()
        return [Contact(**dict(r)) for r in rows]

    def close(self):
        self._conn.close()
```

**Step 2: Create daemon entry point with FastAPI + WebSocket**

```python
# tools/loramsg/daemon.py
"""Magic Messenger daemon — WebSocket API + transport bridge."""

import asyncio
import argparse
import json
import logging
import time
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import uvicorn

from message_store import MessageStore, MessageStatus
from transport_bridge import TransportBridge
from lmx_protocol import LmxMsgType

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s [%(name)s] %(message)s")
log = logging.getLogger("loramsg")

# ── App State ────────────────────────────────────────────────────

app = FastAPI(title="Magic Messenger")
store: MessageStore = None
bridge: TransportBridge = None
ws_clients: set[WebSocket] = set()
my_node_id: int = 0x00  # Will be learned from device

# ── WebSocket Broadcast ──────────────────────────────────────────

async def broadcast(event: str, data: dict):
    msg = json.dumps({"event": event, **data})
    dead = set()
    for ws in ws_clients:
        try:
            await ws.send_text(msg)
        except Exception:
            dead.add(ws)
    ws_clients -= dead

# ── Serial RX Handler (parse firmware responses) ─────────────────

def on_device_rx(line: str):
    """Parse serial lines from firmware for LMX events."""
    # Firmware logs: "LMX RX MSG from XX: "text" (hops=N)"
    if line.startswith("LMX RX MSG from "):
        try:
            parts = line.split('"')
            if len(parts) >= 2:
                text = parts[1]
                src_hex = line.split("from ")[1].split(":")[0].strip()
                src = int(src_hex, 16)
                hops = 0
                if "hops=" in line:
                    hops = int(line.split("hops=")[1].rstrip(")"))
                msg = store.add_incoming(src, my_node_id, text, 0, hops)
                store.upsert_contact(src, hops=hops)
                asyncio.get_event_loop().create_task(
                    broadcast("incoming", msg.to_dict()))
        except Exception as e:
            log.warning(f"Parse error on RX line: {e}")

    # ACK confirmation: "LMX ACK: confirmed pktId=NNNN"
    elif "LMX ACK: confirmed pktId=" in line:
        try:
            pkt_id = int(line.split("pktId=")[1])
            store.update_status(pkt_id, MessageStatus.DELIVERED)
            asyncio.get_event_loop().create_task(
                broadcast("ack", {"packet_id": pkt_id, "status": "delivered"}))
        except Exception as e:
            log.warning(f"Parse error on ACK line: {e}")

    # Failed: "LMX FAIL: pktId=NNNN exhausted"
    elif "LMX FAIL: pktId=" in line:
        try:
            pkt_id = int(line.split("pktId=")[1].split(" ")[0])
            store.update_status(pkt_id, MessageStatus.FAILED)
            asyncio.get_event_loop().create_task(
                broadcast("failed", {"packet_id": pkt_id, "status": "failed"}))
        except Exception as e:
            log.warning(f"Parse error on FAIL line: {e}")

# ── HTTP API ─────────────────────────────────────────────────────

@app.get("/api/messages")
async def get_messages(node: int | None = None, limit: int = 50):
    if node is not None:
        msgs = store.get_conversation(node, limit)
    else:
        msgs = store.get_recent(limit)
    return [m.to_dict() for m in msgs]

@app.get("/api/contacts")
async def get_contacts():
    return [c.to_dict() for c in store.get_contacts()]

@app.post("/api/send")
async def send_message(dest: str, text: str):
    dest_int = int(dest, 16)
    ok = await bridge.send_msg(dest, text)
    if ok:
        # We don't know the packet_id from here (firmware assigns it)
        # For PoC, use timestamp as pseudo-ID
        msg = store.add_outgoing(dest_int, my_node_id, text,
                                 int(time.time() * 1000) & 0xFFFFFFFF)
        await broadcast("outgoing", msg.to_dict())
        return {"status": "sent", "message": msg.to_dict()}
    return {"status": "error", "detail": "transport unavailable"}

@app.get("/api/status")
async def daemon_status():
    return {
        "node_id": f"{my_node_id:02X}",
        "serial_connected": bridge.serial.is_connected if bridge.serial else False,
        "http_host": bridge.http.host if bridge.http else None,
        "contacts": len(store.get_contacts()),
        "messages": len(store.get_recent(1000)),
    }

# ── WebSocket ────────────────────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_clients.add(ws)
    log.info(f"WS client connected ({len(ws_clients)} total)")
    try:
        while True:
            data = await ws.receive_text()
            msg = json.loads(data)
            if msg.get("action") == "send":
                dest = msg["dest"]
                text = msg["text"]
                await send_message(dest, text)
    except WebSocketDisconnect:
        ws_clients.discard(ws)
        log.info(f"WS client disconnected ({len(ws_clients)} remaining)")

# ── Static Files (PWA) ──────────────────────────────────────────

static_dir = Path(__file__).parent / "static"

@app.get("/")
async def index():
    return FileResponse(static_dir / "index.html")

if static_dir.exists():
    app.mount("/static", StaticFiles(directory=str(static_dir)), name="static")

# ── Main ─────────────────────────────────────────────────────────

def main():
    global store, bridge, my_node_id

    parser = argparse.ArgumentParser(description="Magic Messenger Daemon")
    parser.add_argument("--serial", "-s", default=None,
                        help="Serial port (e.g., COM3, /dev/ttyUSB0)")
    parser.add_argument("--http", default=None,
                        help="Device HTTP IP (e.g., 192.168.4.1)")
    parser.add_argument("--port", "-p", type=int, default=8200,
                        help="Daemon port (default: 8200)")
    parser.add_argument("--node-id", "-n", type=lambda x: int(x, 16),
                        default=0x01, help="This node's short ID in hex")
    parser.add_argument("--db", default="loramsg.db",
                        help="SQLite database path")
    args = parser.parse_args()

    my_node_id = args.node_id
    store = MessageStore(args.db)
    bridge = TransportBridge(
        serial_port=args.serial,
        http_host=args.http)
    bridge.on_receive(on_device_rx)

    async def startup():
        await bridge.connect()

    @app.on_event("startup")
    async def app_startup():
        await startup()

    log.info(f"Starting Magic Messenger on port {args.port}")
    log.info(f"Node ID: {my_node_id:02X}")
    uvicorn.run(app, host="0.0.0.0", port=args.port)

if __name__ == "__main__":
    main()
```

**Step 3: Commit**

```bash
git add tools/loramsg/message_store.py tools/loramsg/daemon.py
git commit -m "feat(loramsg): add message store and daemon with WebSocket API"
```

---

## Task 6: PWA Chat Interface

**Files:**
- Create: `tools/loramsg/static/index.html`

**Step 1: Create minimal but functional chat UI**

Create `tools/loramsg/static/index.html` — a single-file PWA with:
- Contact sidebar (click to open conversation)
- Chat thread view (messages with delivery status badges)
- Message input with send button
- Transport status bar (serial/http connected indicator)
- WebSocket connection to daemon for real-time updates
- Responsive layout (works on mobile Safari/Chrome)
- Use Antigravity Suit CSS variables if available, otherwise self-contained dark theme

Key UI elements:
- Header: "LMX Messenger" + node ID + transport status dot
- Left panel: contact list with last-seen times
- Right panel: chat messages (incoming left, outgoing right)
- Bottom: text input + send button + "new message to hex ID" option
- Message bubbles show: text, timestamp, status icon (clock/check/double-check/X)

CSS should be self-contained (no external deps). Use CSS custom properties for theming.
JS connects via `new WebSocket('ws://localhost:8200/ws')` and handles events:
- `incoming`: append message to chat, play notification sound (optional)
- `outgoing`: append own message
- `ack`: update status icon to delivered
- `failed`: update status icon to failed

**Step 2: Commit**

```bash
git add tools/loramsg/static/index.html
git commit -m "feat(loramsg): add PWA chat interface"
```

---

## Task 7: Integration — Build, Flash, Test End-to-End

**Step 1: Build firmware**

Run: `pio run -e heltec_wifi_lora_32_V3`
Expected: Clean compile with MsgManager integrated

**Step 2: Flash to both Heltec devices**

Run: `pio run -e ota_master` (master device)
Run: `pio run -e ota_slave` (slave device)

**Step 3: Test MSG command via Serial**

Connect to master via Serial Monitor:
```
MSG STATUS
```
Expected: `MSG: tx=0 rx=0 pending=0`

```
MSG SEND <slave_short_id_hex> Hello from master
```
Expected: `MSG: queued for XX`

On slave Serial Monitor, expected:
```
LMX RX MSG from <master_id>: "Hello from master" (hops=0)
```

On master, expected (after ACK):
```
LMX ACK: confirmed pktId=XXXX
```

**Step 4: Start PC daemon**

```bash
cd tools/loramsg
pip install -r requirements.txt
python daemon.py --serial COM3 --node-id 01
```
Expected: Server starts on port 8200

**Step 5: Open PWA and send message**

Open browser: `http://localhost:8200`
- Type destination hex ID
- Send "Hello via LMX"
- Verify message appears in chat with sent status
- Verify ACK updates status to delivered

**Step 6: Final commit**

```bash
git add -A
git commit -m "feat(lmx): Magic Messenger PoC — end-to-end text over LoRa mesh"
```

---

## Summary

| Task | Component | Files | Est. Lines |
|------|-----------|-------|------------|
| 1 | LMX Header/Structs | MsgManager.h | ~100 |
| 2 | MsgManager Core | MsgManager.cpp | ~250 |
| 3 | Firmware Wiring | main.cpp, ScheduleManager, LoRaManager | ~30 |
| 4 | Protocol + Transport | lmx_protocol.py, transport_bridge.py | ~200 |
| 5 | Store + Daemon | message_store.py, daemon.py | ~250 |
| 6 | PWA Chat UI | static/index.html | ~400 |
| 7 | Integration Test | (no new files) | 0 |
| **Total** | | **8 new files + 3 modified** | **~1230** |
