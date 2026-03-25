/**
 * @file message_handler.h
 * @brief Dispatch logic for incoming LoRaLink packets
 */

#pragma once

#include <Arduino.h>
#include "control_packet.h"
#include "../Transport/interface.h"

class MessageHandler {
public:
    static void handleReceived(TransportType transportType, const uint8_t* payload, size_t len);

private:
    static void handleAction(const ControlPacket* pkt);
    static void handleTelemetry(const ControlPacket* pkt);
    static void handleAck(const ControlPacket* pkt);
    static void handleHeartbeat(const ControlPacket* pkt);
};
