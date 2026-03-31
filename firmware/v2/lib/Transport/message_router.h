/**
 * @file message_router.h
 * @brief Message Router Singleton (Any-to-Any Packet Dispatch)
 *
 * Central hub for all inter-transport communication in Magic v2.
 * Routes raw bytes between active transports (LoRa, MQTT, Serial, BLE, etc.)
 * and feeds packets to the application layer.
 */

#pragma once

#include "interface.h"
#include <vector>
#include <functional>

// ============================================================================
// Message Handler Callback Type
// ============================================================================

/**
 * @typedef MessageHandlerCallback
 * @brief Callback function type for message reception
 * @param transportType Source transport type
 * @param payload Received data buffer
 * @param len Number of bytes
 */
typedef void (*MessageHandlerCallback)(
  TransportType transportType,
  const uint8_t* payload,
  size_t len
);

// ============================================================================
// Message Router Singleton
// ============================================================================

/**
 * @class MessageRouter
 * @brief Central packet router and dispatcher
 *
 * Responsibilities:
 * 1. Register active transports
 * 2. Poll all transports for incoming packets
 * 3. Route packets between transports based on destination
 * 4. Call application-level message handlers
 * 5. Handle broadcast packets
 *
 * All operations are non-blocking and thread-safe (single-threaded assumed).
 */
class MessageRouter {
public:
  /**
   * @brief Get singleton instance
   * @return Reference to global MessageRouter
   */
  static MessageRouter& instance();

  // ========================================================================
  // Transport Registration
  // ========================================================================

  /**
   * @brief Register an active transport
   * @param transport Pointer to TransportInterface implementation
   * @return true on success, false if already registered
   */
  bool registerTransport(TransportInterface* transport);

  /**
   * @brief Unregister a transport
   * @param transportType Type of transport to remove
   * @return true if removed, false if not found
   */
  bool unregisterTransport(TransportType transportType);

  /**
   * @brief Get registered transport by type
   * @param type TransportType enum value
   * @return Pointer to TransportInterface, or nullptr if not found
   */
  TransportInterface* getTransport(TransportType type);

  /**
   * @brief Get number of registered transports
   * @return Count of active transports
   */
  size_t getTransportCount() const { return _transports.size(); }

  // ========================================================================
  // Message Processing & Dispatch
  // ========================================================================

  /**
   * @brief Poll all transports for incoming messages
   * Called periodically from main loop or FreeRTOS task.
   * Processes RX from all transports and dispatches to registered handlers.
   */
  void process();

  /**
   * @brief Register application message handler
   * Called when packets arrive from any transport.
   * @param callback Function pointer to message handler
   */
  void setMessageHandler(MessageHandlerCallback callback);

  // ========================================================================
  // Broadcast & Unicast Send
  // ========================================================================

  /**
   * @brief Broadcast packet to all registered transports
   * @param payload Raw packet data
   * @param len Packet size
   * @return true if broadcast to at least one transport
   */
  bool broadcastPacket(const uint8_t* payload, size_t len);

  /**
   * @brief Send to specific transport by type
   * @param transportType Target transport type
   * @param payload Raw data buffer
   * @param len Packet length
   * @return Bytes sent, or negative TransportStatus code
   */
  int sendTo(TransportType transportType, const uint8_t* payload, size_t len);

  /**
   * @brief Send to first available transport
   * Tries registered transports in priority order (LoRa > MQTT > Serial).
   * @param payload Raw data buffer
   * @param len Packet length
   * @return Bytes sent, or negative error code
   */
  int sendToAny(const uint8_t* payload, size_t len);

  // ========================================================================
  // Router Statistics & Diagnostics
  // ========================================================================

  /**
   * @brief Get total packets routed since init
   * @return Packet count
   */
  uint32_t getTotalPacketsRouted() const { return _packetsProcessed; }

  /**
   * @brief Get total bytes routed
   * @return Total bytes
   */
  uint32_t getTotalBytesRouted() const { return _bytesProcessed; }

  /**
   * @brief Get dropped packets count
   * @return Dropped packet count
   */
  uint32_t getDroppedPackets() const { return _droppedPackets; }

  /**
   * @brief Get router status string
   * @return Human-readable status
   */
  const char* getStatus() const;

  /**
   * @brief Clear all statistics
   */
  void clearStats();

private:
  // Private constructor for singleton
  MessageRouter() = default;

  // Registered transports (max 8 for practical mesh)
  std::vector<TransportInterface*> _transports;

  // Application message handler
  MessageHandlerCallback _messageHandler = nullptr;

  // Statistics
  uint32_t _packetsProcessed = 0;
  uint32_t _bytesProcessed = 0;
  uint32_t _droppedPackets = 0;

  // RX work buffer (temporary, reused each poll cycle)
  uint8_t _rxBuffer[256];

  // Helper: process RX from a single transport
  void _processTransportRx(TransportInterface* transport);
};

// ============================================================================
// Global Instance Access
// ============================================================================

extern MessageRouter& messageRouter;

inline MessageRouter& getMessageRouter() {
  return MessageRouter::instance();
}
