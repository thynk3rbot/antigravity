/**
 * @file message_router.cpp
 * @brief Message Router Implementation
 */

#include "message_router.h"
#include <Arduino.h>
#include <cstring>

// Static instance
MessageRouter& messageRouter = MessageRouter::instance();

// ============================================================================
// Singleton Implementation
// ============================================================================

MessageRouter& MessageRouter::instance() {
  static MessageRouter _instance;
  return _instance;
}

// ============================================================================
// Transport Registration
// ============================================================================

bool MessageRouter::registerTransport(TransportInterface* transport) {
  if (!transport) {
    return false;
  }

  // Check if already registered
  for (auto& t : _transports) {
    if (t->getType() == transport->getType()) {
      return false;  // Already registered
    }
  }

  // Initialize transport
  if (!transport->init()) {
    return false;
  }

  _transports.push_back(transport);
  Serial.printf("[MessageRouter] Registered transport: %s\n", transport->getName());
  return true;
}

bool MessageRouter::unregisterTransport(TransportType transportType) {
  for (auto it = _transports.begin(); it != _transports.end(); ++it) {
    if ((*it)->getType() == transportType) {
      (*it)->shutdown();
      _transports.erase(it);
      Serial.printf("[MessageRouter] Unregistered transport type %d\n",
                    static_cast<int>(transportType));
      return true;
    }
  }
  return false;
}

TransportInterface* MessageRouter::getTransport(TransportType type) {
  for (auto t : _transports) {
    if (t->getType() == type) {
      return t;
    }
  }
  return nullptr;
}

// ============================================================================
// Message Processing
// ============================================================================

void MessageRouter::process() {
  for (auto transport : _transports) {
    _processTransportRx(transport);
  }
}

void MessageRouter::_processTransportRx(TransportInterface* transport) {
  if (!transport || !transport->isReady()) {
    return;
  }

  int len = transport->recv(_rxBuffer, sizeof(_rxBuffer));
  if (len > 0) {
    _packetsProcessed++;
    _bytesProcessed += len;

    Serial.printf("[MessageRouter] Received %d bytes from %s\n",
                  len, transport->getName());

    // Call application handler if registered
    if (_messageHandler) {
      _messageHandler(transport->getType(), _rxBuffer, len);
    }
  } else if (len < 0) {
    _droppedPackets++;
    Serial.printf("[MessageRouter] Error from %s: %d\n",
                  transport->getName(), len);
  }
}

void MessageRouter::setMessageHandler(MessageHandlerCallback callback) {
  _messageHandler = callback;
  if (callback) {
    Serial.println("[MessageRouter] Message handler registered");
  }
}

// ============================================================================
// Broadcast & Unicast
// ============================================================================

bool MessageRouter::broadcastPacket(const uint8_t* payload, size_t len) {
  if (!payload || len == 0 || _transports.empty()) {
    return false;
  }

  bool sentAny = false;
  for (auto transport : _transports) {
    if (transport->isReady()) {
      int bytesSent = transport->send(payload, len);
      if (bytesSent > 0) {
        sentAny = true;
        Serial.printf("[MessageRouter] Broadcast to %s: %d bytes\n",
                      transport->getName(), bytesSent);
      }
    }
  }

  return sentAny;
}

int MessageRouter::sendTo(TransportType transportType,
                         const uint8_t* payload, size_t len) {
  TransportInterface* transport = getTransport(transportType);
  if (!transport || !transport->isReady()) {
    return -1;
  }

  int bytesSent = transport->send(payload, len);
  if (bytesSent > 0) {
    Serial.printf("[MessageRouter] Send to %s: %d bytes\n",
                  transport->getName(), bytesSent);
  }
  return bytesSent;
}

int MessageRouter::sendToAny(const uint8_t* payload, size_t len) {
  if (!payload || len == 0 || _transports.empty()) {
    return -1;
  }

  // Try LoRa first (primary), then others
  TransportInterface* loraTransport = getTransport(TransportType::LORA);
  if (loraTransport && loraTransport->isReady()) {
    return loraTransport->send(payload, len);
  }

  // Fallback to first available transport
  for (auto transport : _transports) {
    if (transport->isReady()) {
      return transport->send(payload, len);
    }
  }

  return -1;
}

// ============================================================================
// Statistics & Diagnostics
// ============================================================================

const char* MessageRouter::getStatus() const {
  static char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "Router: %zu transports, %lu packets, %lu bytes, %lu dropped",
           _transports.size(), _packetsProcessed, _bytesProcessed,
           _droppedPackets);
  return buffer;
}

void MessageRouter::clearStats() {
  _packetsProcessed = 0;
  _bytesProcessed = 0;
  _droppedPackets = 0;
  Serial.println("[MessageRouter] Statistics cleared");
}
