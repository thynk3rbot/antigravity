#pragma once

#include <Arduino.h>
#include <functional>
#include "../Transport/interface.h"

// ── LMX Protocol Constants ───────────────────────────────────────────
#define LMX_SYNC_0              0xAA
#define LMX_SYNC_1              0x4D
#define LMX_HEADER_SIZE         12
#define LMX_MAX_PAYLOAD         204    // leaves room for 6-byte frag header + encryption overhead
#define LMX_MAX_PACKET          (LMX_HEADER_SIZE + LMX_MAX_PAYLOAD)
#define LMX_BROADCAST           0xFF
#define LMX_DEDUP_SIZE          64
#define LMX_DEDUP_TTL_MS        300000UL
#define LMX_MAX_RETRIES         3
#define LMX_ACK_TIMEOUT_MS      5000UL

// Fragmentation
#define LMX_FRAG_HDR_SIZE       6      // [index:1][count:1][orig_id:4]
#define LMX_FRAG_DATA_SIZE      (LMX_MAX_PAYLOAD - LMX_FRAG_HDR_SIZE)
#define LMX_MAX_FRAGMENTS       8
#define LMX_MAX_MSG_BYTES       (LMX_FRAG_DATA_SIZE * LMX_MAX_FRAGMENTS)  // ~1584 bytes
#define LMX_REASSEMBLY_TIMEOUT  10000UL
#define LMX_REASSEMBLY_SLOTS    4

// Transport priority (index = priority, 0 = highest)
#define LMX_TRANSPORT_COUNT     4

enum class LmxMsgType : uint8_t {
    TEXT          = 0x0,
    ACK           = 0x1,
    NACK          = 0x2,
    NODE_ANNOUNCE = 0x6,
    PING          = 0x7,
    FRAGMENT      = 0x8,
};

// ── LMX Packet Header (12 bytes) ─────────────────────────────────────
struct __attribute__((packed)) LmxHeader {
    uint8_t  sync[2];       // {0xAA, 0x4D}
    uint8_t  dest;           // Destination short ID (0xFF = broadcast)
    uint8_t  src;            // Original sender short ID
    uint32_t packetId;       // Unique message ID
    uint8_t  flags;          // [HopLimit:3][WantAck:1][MsgType:4]
    uint8_t  hopStart;       // Original hop limit
    uint8_t  _reserved[2];

    uint8_t    hopLimit() const { return (flags >> 5) & 0x07; }
    bool       wantAck()  const { return (flags >> 4) & 0x01; }
    LmxMsgType msgType()  const { return (LmxMsgType)(flags & 0x0F); }

    void setFlags(uint8_t hops, bool ack, LmxMsgType type) {
        flags = ((hops & 0x07) << 5) | ((ack ? 1 : 0) << 4) | ((uint8_t)type & 0x0F);
    }
};
static_assert(sizeof(LmxHeader) == LMX_HEADER_SIZE, "LmxHeader size mismatch");

// ── Fragment Header (6 bytes, payload of FRAGMENT packets) ───────────
struct __attribute__((packed)) LmxFragHeader {
    uint8_t  fragIndex;     // 0..N-1
    uint8_t  fragCount;     // total fragments in this message
    uint32_t origPacketId;  // packet_id of first fragment
};
static_assert(sizeof(LmxFragHeader) == LMX_FRAG_HDR_SIZE, "LmxFragHeader size mismatch");

// ── Reassembly Slot ───────────────────────────────────────────────────
struct LmxReassemblySlot {
    bool          active;
    uint8_t       src;
    uint32_t      origPacketId;
    uint8_t       fragCount;
    uint8_t       received;                                    // bitmask of received fragments
    uint8_t       data[LMX_MAX_FRAGMENTS][LMX_FRAG_DATA_SIZE];
    uint8_t       dataLen[LMX_MAX_FRAGMENTS];
    unsigned long lastFragMs;
};

// ── Dedup Cache Entry ─────────────────────────────────────────────────
struct LmxDedupEntry {
    uint8_t  src;
    uint32_t packetId;
    unsigned long seenMs;
};

// ── Pending ACK (reliable delivery) ──────────────────────────────────
struct LmxPendingAck {
    bool     active;
    uint8_t  dest;
    uint32_t packetId;
    uint8_t  packet[LMX_MAX_PACKET];
    size_t   packetLen;
    int      retryCount;
    unsigned long lastAttemptMs;
};

// ── MsgManager Singleton ──────────────────────────────────────────────
class MsgManager {
public:
    static MsgManager& getInstance() {
        static MsgManager instance;
        return instance;
    }

    void init();

    // Send a text message (fragments automatically if > LMX_FRAG_DATA_SIZE)
    bool sendText(uint8_t dest, const String& text, bool wantAck = true);

    // Process a raw LMX packet from any transport
    void handleLmxPacket(const uint8_t* data, size_t len, TransportType source);

    // Tick — call from periodic task (~50ms)
    void tick();

    // Callback fired when a complete message arrives (after reassembly if fragmented)
    typedef std::function<void(uint8_t src, const String& text, int hopsUsed)> MsgCallback;
    void setOnMessage(MsgCallback cb) { _onMessage = cb; }

    uint32_t getTxCount() const { return _txCount; }
    uint32_t getRxCount() const { return _rxCount; }

private:
    MsgManager();
    MsgManager(const MsgManager&) = delete;
    MsgManager& operator=(const MsgManager&) = delete;

    // Core TX
    bool _sendLmxPacket(uint8_t dest, LmxMsgType type, bool wantAck,
                        const uint8_t* payload, size_t payloadLen,
                        TransportType preferredTransport = TransportType::LORA);
    bool _sendFragmented(uint8_t dest, const uint8_t* data, size_t len, bool wantAck);
    void _sendAck(uint8_t dest, uint32_t originalPacketId, TransportType via);
    void _rebroadcast(const uint8_t* raw, size_t len);

    // Transport selection
    TransportType _selectTransport(uint8_t dest);

    // Dedup
    bool _isDuplicate(uint8_t src, uint32_t packetId);
    void _markSeen(uint8_t src, uint32_t packetId);

    // Retry engine
    void _checkRetries();

    // Reassembly
    void _handleFragment(const LmxHeader* hdr, const uint8_t* payload,
                         size_t payloadLen, TransportType source);
    void _checkReassemblyTimeouts();

    static constexpr int MAX_PENDING = 5;
    LmxPendingAck        _pending[MAX_PENDING];
    LmxDedupEntry        _dedup[LMX_DEDUP_SIZE];
    LmxReassemblySlot    _reassembly[LMX_REASSEMBLY_SLOTS];
    int                  _dedupHead = 0;

    uint32_t _nextPacketId = 1;
    uint32_t _txCount = 0;
    uint32_t _rxCount = 0;

    MsgCallback _onMessage = nullptr;
};
