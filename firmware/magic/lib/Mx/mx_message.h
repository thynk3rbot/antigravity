// firmware/magic/lib/Mx/mx_message.h
#pragma once
#include <cstdint>
#include <cstddef>

/**
 * MxOp — Universal operation types for the Mx Framework.
 */
enum class MxOp : uint8_t {
    UPDATE = 0,       // field-level merge into existing record
    INSERT,           // new record creation
    REMOVE,           // record deletion
    SUBSCRIBE,        // register interest in a subject
    UNSUBSCRIBE,      // deregister interest
    EXECUTE,          // command execution
    WALK              // enumerate all records
};

/**
 * MxMessage — Fixed-size container for internal bus messaging.
 * Payload size fits one LoRa frame (256 bytes) minus header.
 */
constexpr size_t MX_PAYLOAD_MAX = 246;

struct MxMessage {
    MxOp op;                    // 1 byte
    uint8_t src_transport;      // 1 byte (enum)
    uint16_t subject_id;        // 2 bytes
    uint8_t payload[MX_PAYLOAD_MAX]; // 246 bytes
    uint8_t payload_len;        // 1 byte
};

#include "mx_subjects.h"
