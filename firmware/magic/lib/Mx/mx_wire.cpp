#include "mx_wire.h"
#include <cstring>

uint8_t MxWire::serialize(const MxMessage& msg, uint8_t* buffer, uint8_t node_id_high, uint8_t node_id_low, uint8_t sequence, uint8_t ttl) {
    if (!buffer) return 0;

    buffer[0] = static_cast<uint8_t>(msg.op);
    buffer[1] = node_id_high;
    buffer[2] = node_id_low;
    buffer[3] = (msg.subject_id >> 8) & 0xFF;
    buffer[4] = msg.subject_id & 0xFF;
    buffer[5] = sequence;
    buffer[6] = ttl;
    buffer[7] = msg.payload_len;

    if (msg.payload_len > 0) {
        std::memcpy(&buffer[8], msg.payload, msg.payload_len);
    }

    uint8_t total_len = 8 + msg.payload_len;
    uint16_t crc = calculateCRC(buffer, total_len);
    
    buffer[total_len] = (crc >> 8) & 0xFF;
    buffer[total_len + 1] = crc & 0xFF;

    return total_len + 2;
}

bool MxWire::deserialize(const uint8_t* data, uint8_t len, MxMessage* out_msg) {
    if (!data || len < 10 || !out_msg) return false;

    // CRC Check (last 2 bytes)
    uint16_t sent_crc = (data[len - 2] << 8) | data[len - 1];
    if (calculateCRC(data, len - 2) != sent_crc) return false;

    out_msg->op = static_cast<MxOp>(data[0]);
    // 1-2: node_id (internal use usually for routing, could map to src_transport or context)
    out_msg->subject_id = (data[3] << 8) | data[4];
    // 5: sequence, 6: ttl (mesh specific)
    out_msg->payload_len = data[7];
    
    if (out_msg->payload_len > 0 && out_msg->payload_len <= 252) {
        std::memcpy(out_msg->payload, &data[8], out_msg->payload_len);
    } else if (out_msg->payload_len > 252) {
        return false;
    }

    return true;
}

uint16_t MxWire::calculateCRC(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
