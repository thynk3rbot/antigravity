#include "message_handler.h"
#include "mesh_coordinator.h"
#include "command_manager.h"
#include "hal_compat.h"
#include "msg_manager.h"
#include "../HAL/relay_hal.h"
#include "../HAL/mcp_manager.h"
#include "../Transport/message_router.h"
#include "../Transport/lora_transport.h"
#include "../Transport/espnow_transport.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern uint8_t g_ourNodeID;

void MessageHandler::handleReceived(TransportType transportType, const uint8_t* payload, size_t len) {
    // Detect LMX packets by sync bytes before ControlPacket dispatch
    if (len >= 2 && payload[0] == 0xAA && payload[1] == 0x4D) {
        MsgManager::getInstance().handleLmxPacket(payload, len, transportType);
        return;
    }

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
        case PacketType::GPIO_SET:
            handleGpioSet(pkt);
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

void MessageHandler::handleGpioSet(const ControlPacket* pkt) {
    if (pkt->header.dest == g_ourNodeID || pkt->header.dest == 0xFF) {
        uint8_t pin = pkt->payload.gpio.pin;
        uint8_t action = pkt->payload.gpio.action;
        uint16_t duration = pkt->payload.gpio.duration_ms;

        Serial.printf("[GPIO] Peer %u cmd: Pin %d, Action %d, Dur %dms\n",
                      pkt->header.src, pin, action, duration);

        if (pin > 49 && pin < 100) {
            Serial.println("  ! Invalid pin (50-99 reserved)");
            return;
        }

        uPinMode(pin, OUTPUT);
        
        bool targetLevel = LOW;
        if (action == 1) { // ON/HIGH
            targetLevel = HIGH;
        } else if (action == 2) { // TOGGLE
            targetLevel = (uDigitalRead(pin) == HIGH) ? LOW : HIGH;
        }

        uDigitalWrite(pin, targetLevel);

        if (duration > 0) {
            // Simple pulsed output (blocking for now, or use a timer task for robustness)
            // For sovereignty phase, simple blocking is okay for testing
            vTaskDelay(pdMS_TO_TICKS(duration));
            uDigitalWrite(pin, !targetLevel);
        }

        if (pkt->header.requiresACK()) {
            ControlPacket ack = ControlPacket::makeACK(
                g_ourNodeID, pkt->header.src, pkt->header.seq
            );
            MessageRouter::instance().broadcastPacket((uint8_t*)&ack, sizeof(ack));
        }
    }
}
