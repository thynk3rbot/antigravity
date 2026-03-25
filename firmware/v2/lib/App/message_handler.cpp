/**
 * @file message_handler.cpp
 * @brief Implementation of packet dispatch logic
 */

#include "message_handler.h"
#include "mesh_coordinator.h"
#include "command_manager.h"
#include "../HAL/relay_hal.h"
#include "../Transport/message_router.h"
#include "../Transport/lora_transport.h"
#include "../Transport/espnow_transport.h"
#include <Arduino.h>

extern uint8_t g_ourNodeID;

void MessageHandler::handleReceived(TransportType transportType, const uint8_t* payload, size_t len) {
    if (len < sizeof(ControlPacket)) {
        if (MeshCoordinator::instance().handleV1Packet(payload, len)) {
            return; 
        }
        return; 
    }

    const ControlPacket* pkt = reinterpret_cast<const ControlPacket*>(payload);

    switch (static_cast<PacketType>(pkt->header.type)) {
        case PacketType::ACTION:
            handleAction(pkt);
            break;
        case PacketType::TELEMETRY:
            handleTelemetry(pkt);
            break;
        case PacketType::ACK:
            handleAck(pkt);
            break;
        case PacketType::HEARTBEAT:
            handleHeartbeat(pkt);
            break;
        default:
            Serial.printf("[UNKNOWN] Packet type 0x%02X\n", pkt->header.type);
            break;
    }
}

void MessageHandler::handleAction(const ControlPacket* pkt) {
    if (pkt->header.dest == g_ourNodeID || pkt->header.dest == 0xFF) {
        Serial.printf("[ACTION] Toggle relays: mask=0x%02X, state=%d\n",
                      pkt->payload.action.relayMask,
                      pkt->payload.action.relayState);

        uint8_t currentState = RelayHAL::getInstance().getState();
        uint8_t newState = currentState;

        for (int i = 0; i < 8; i++) {
            if (pkt->payload.action.relayMask & (1 << i)) {
                if (pkt->payload.action.relayState) {
                    newState |= (1 << i);
                } else {
                    newState &= ~(1 << i);
                }
            }
        }

        RelayHAL::getInstance().setState(newState);

        if (pkt->header.requiresACK()) {
            ControlPacket ack = ControlPacket::makeACK(
                g_ourNodeID, pkt->header.src, pkt->header.seq
            );
            MessageRouter::instance().broadcastPacket((uint8_t*)&ack, sizeof(ack));
            Serial.printf("[ACK] Sent for seq=%u\n", (unsigned int)pkt->header.seq);
        }
    }
}

void MessageHandler::handleTelemetry(const ControlPacket* pkt) {
    Serial.printf("[TELEMETRY] From Peer %u: Temp=%.1f°C, V=%.2fV, Relays=0x%02X\n",
                  pkt->header.src,
                  (float)pkt->payload.telemetry.tempC_x10 / 10.0f,
                  (float)pkt->payload.telemetry.voltageV_x100 / 100.0f,
                  pkt->payload.telemetry.relayState);
}

void MessageHandler::handleAck(const ControlPacket* pkt) {
    Serial.printf("[ACK_RX] From node %u, seq=%u\n",
                  pkt->header.src, pkt->header.seq);
}

void MessageHandler::handleHeartbeat(const ControlPacket* pkt) {
    Serial.printf("[HEARTBEAT] From node %u\n", pkt->header.src);
    // Auto-update neighbor table
    MeshCoordinator::instance().updateNeighbor(pkt->header.src, 0, 1); // RSSI logic handled in RadioTask
}
