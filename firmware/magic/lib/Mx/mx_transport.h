#pragma once
#include "mx_message.h"

/**
 * MxTransport — Abstract interface for wire-level communication.
 * Bridges wire interface (LoRa, MQTT, WiFi) with the internal MxBus.
 */
class MxTransport {
public:
    virtual ~MxTransport() = default;

    // Transport name (for logging/diagnostics)
    virtual const char* name() const = 0;

    // Send a message out over this transport
    virtual bool send(const MxMessage& msg) = 0;

    // Called when data arrives from the wire
    virtual void onReceive(const uint8_t* data, uint8_t len) = 0;

    // Lifecycle
    virtual bool init() = 0;
    virtual void shutdown() = 0;
};
