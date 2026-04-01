#pragma once
#include "mx_message.h"

/**
 * MxWire — Serializer/Deserializer for the LoRa binary protocol.
 * Implementation follows strictly defined mesh protocol bytes.
 */
class MxWire {
public:
    // Serialize an MxMessage into a LoRa-ready buffer.
    // Returns actual number of bytes written.
    static uint8_t serialize(const MxMessage& msg, uint8_t* buffer, uint8_t node_id_high, uint8_t node_id_low, uint8_t sequence, uint8_t ttl);

    // Deserialize a buffer into an MxMessage.
    static bool deserialize(const uint8_t* data, uint8_t len, MxMessage* out_msg);

private:
    static uint16_t calculateCRC(const uint8_t* data, uint8_t len);
};
