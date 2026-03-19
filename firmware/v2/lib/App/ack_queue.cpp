#include "ack_queue.h"

// Static member definitions
PendingPacket ACKQueue::_queue[ACKQueue::MAX_PENDING];
uint8_t  ACKQueue::_count        = 0;
uint32_t ACKQueue::_totalRetries = 0;
uint32_t ACKQueue::_totalDropped = 0;

void ACKQueue::begin() {
    _count        = 0;
    _totalRetries = 0;
    _totalDropped = 0;
}

bool ACKQueue::enqueue(const ControlPacket& pkt, const String& dest, uint32_t seqNum) {
    if (_count >= MAX_PENDING) {
        return false;  // Queue full
    }
    PendingPacket& slot = _queue[_count++];
    slot.packet     = pkt;
    slot.destination = dest;
    slot.sentAt     = millis();
    slot.retryCount = 0;
    slot.seqNum     = seqNum;
    return true;
}

void ACKQueue::acknowledge(uint32_t seqNum) {
    for (uint8_t i = 0; i < _count; i++) {
        if (_queue[i].seqNum == seqNum) {
            // Remove by shifting remaining entries left
            for (uint8_t j = i; j < _count - 1; j++) {
                _queue[j] = _queue[j + 1];
            }
            _count--;
            return;
        }
    }
}

void ACKQueue::tick(RetrySender sender) {
    uint32_t now = millis();
    uint8_t i = 0;
    while (i < _count) {
        PendingPacket& entry = _queue[i];
        if ((now - entry.sentAt) > ACK_TIMEOUT_MS) {
            if (entry.retryCount >= MAX_RETRIES) {
                // Drop — max retries exceeded
                _totalDropped++;
                for (uint8_t j = i; j < _count - 1; j++) {
                    _queue[j] = _queue[j + 1];
                }
                _count--;
                // Do NOT increment i — slot[i] is now the next entry
            } else {
                // Retry
                entry.retryCount++;
                entry.sentAt = now;
                _totalRetries++;
                sender(entry.packet, entry.destination);
                i++;
            }
        } else {
            i++;
        }
    }
}

uint8_t ACKQueue::pendingCount() {
    return _count;
}

uint32_t ACKQueue::totalRetries() {
    return _totalRetries;
}

uint32_t ACKQueue::totalDropped() {
    return _totalDropped;
}
