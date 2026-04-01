#pragma once
#include <Arduino.h>
#include <functional>
#include "control_packet.h"

struct PendingPacket {
    ControlPacket packet;
    String destination;
    uint32_t sentAt;
    uint8_t retryCount;
    uint32_t seqNum;
};

class ACKQueue {
public:
    static constexpr uint8_t  MAX_PENDING     = 8;
    static constexpr uint8_t  MAX_RETRIES     = 3;
    static constexpr uint32_t ACK_TIMEOUT_MS  = 5000;

    static void begin();

    // Add packet awaiting ACK
    static bool enqueue(const ControlPacket& pkt, const String& dest, uint32_t seqNum);

    // Call when ACK received — removes matching entry
    static void acknowledge(uint32_t seqNum);

    // Call in main loop — retries timed-out packets
    using RetrySender = std::function<void(const ControlPacket&, const String&)>;
    static void tick(RetrySender sender);

    // Stats
    static uint8_t  pendingCount();
    static uint32_t totalRetries();
    static uint32_t totalDropped();

private:
    static PendingPacket _queue[MAX_PENDING];
    static uint8_t  _count;
    static uint32_t _totalRetries;
    static uint32_t _totalDropped;
};
