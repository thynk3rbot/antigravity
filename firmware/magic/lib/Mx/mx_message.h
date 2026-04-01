#pragma once
#include <cstdint>

/**
 * MxOp — The seven fundamental operations of the Mx framework.
 */
enum class MxOp : uint8_t {
    UPDATE = 0,       // Field-level merge into existing record
    INSERT,           // New record creation
    REMOVE,           // Record deletion
    SUBSCRIBE,        // Register interest in a subject
    UNSUBSCRIBE,      // Deregister interest
    EXECUTE,          // Command execution (STATUS, RELAY, GPIO, etc.)
    WALK              // Enumerate all records in a cache
};

/**
 * MxMessage — Universal message container for the Mx bus.
 * Sized to fit exactly into a single LoRa frame (256 bytes) including header.
 */
struct MxMessage {
    MxOp op;                    // 1 byte — operation type
    uint8_t src_transport;      // 1 byte — which transport sent this
    uint16_t subject_id;        // 2 bytes — subject identifier
    uint8_t payload[252];       // payload — sized to LoRa max
    uint8_t payload_len;        // actual payload length
};
// Total size: 257 bytes. In practice, usually allocated in a pool.
