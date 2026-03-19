/**
 * @file interface.h
 * @brief Abstract Transport Interface for LoRaLink v2
 *
 * Defines the base interface for all transport layers (LoRa, MQTT, BLE, Serial).
 * All transports inherit from this interface and are managed by MessageRouter.
 */

#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================================
// Transport Status Codes
// ============================================================================

enum class TransportStatus : int {
  OK              = 0,
  NOT_READY       = -1,
  TX_BUSY         = -2,
  RX_TIMEOUT      = -3,
  CRC_ERROR       = -4,
  BUFFER_FULL     = -5,
  INVALID_ARG     = -6,
  INIT_ERROR      = -100,
};

// ============================================================================
// Transport Type Enumeration
// ============================================================================

enum class TransportType : uint8_t {
  LORA = 0,
  MQTT = 1,
  BLE = 2,
  SERIAL_DEBUG = 3,
  ESPNOW = 4,  // Reserved for future use
  HTTP = 5,    // Reserved for future use
  UNKNOWN = 255
};

// ============================================================================
// Abstract Transport Interface
// ============================================================================

/**
 * @class TransportInterface
 * @brief Base class for all transport layers
 *
 * Defines the contract that all transport implementations must fulfill.
 * MessageRouter communicates exclusively through this interface.
 */
class TransportInterface {
public:
  virtual ~TransportInterface() = default;

  // ========================================================================
  // Core Transport Operations
  // ========================================================================

  /**
   * @brief Initialize the transport layer
   * @return true on success, false on failure
   */
  virtual bool init() = 0;

  /**
   * @brief Shut down the transport cleanly
   */
  virtual void shutdown() {}

  /**
   * @brief Check if transport is ready to send/receive
   * @return true if operational, false otherwise
   */
  virtual bool isReady() const = 0;

  /**
   * @brief Transmit raw bytes
   * @param payload Pointer to data buffer
   * @param len Number of bytes to send
   * @return Number of bytes sent, or negative TransportStatus code
   */
  virtual int send(const uint8_t* payload, size_t len) = 0;

  /**
   * @brief Receive raw bytes (non-blocking)
   * @param buffer Output buffer
   * @param maxLen Maximum bytes to read
   * @return Number of bytes received, 0 if no data, or negative TransportStatus code
   */
  virtual int recv(uint8_t* buffer, size_t maxLen) = 0;

  /**
   * @brief Check if data is available without blocking
   * @return true if packet ready, false otherwise
   */
  virtual bool isAvailable() const = 0;

  /**
   * @brief Poll the transport (called periodically by MessageRouter)
   * Performs any internal state updates, background tasks, etc.
   */
  virtual void poll() {}

  // ========================================================================
  // Transport Identity & Diagnostics
  // ========================================================================

  /**
   * @brief Get transport name
   * @return Null-terminated string (e.g., "LoRa", "MQTT", "BLE")
   */
  virtual const char* getName() const = 0;

  /**
   * @brief Get transport type
   * @return TransportType enum value
   */
  virtual TransportType getType() const = 0;

  /**
   * @brief Get human-readable status
   * @return Status string (e.g., "Connected", "Waiting", "Error")
   */
  virtual const char* getStatus() const = 0;

  /**
   * @brief Get signal strength (if applicable)
   * @return RSSI in dBm for radio transports, 0 for wired
   */
  virtual int8_t getSignalStrength() const { return 0; }

  /**
   * @brief Get bytes sent since init
   * @return Total bytes transmitted
   */
  virtual uint32_t getTxBytes() const { return 0; }

  /**
   * @brief Get bytes received since init
   * @return Total bytes received
   */
  virtual uint32_t getRxBytes() const { return 0; }

  // ========================================================================
  // Error Handling
  // ========================================================================

  /**
   * @brief Get last error code
   * @return Error code (0 = no error)
   */
  virtual int getLastError() const { return 0; }

  /**
   * @brief Clear error state
   */
  virtual void clearError() {}

  /**
   * @brief Get last error message
   * @return Null-terminated error string
   */
  virtual const char* getLastErrorString() const { return "No error"; }
};

// ============================================================================
// Transport Factory (optional convenience)
// ============================================================================

/**
 * @brief Create a transport instance by type
 * @param type TransportType enum value
 * @return Pointer to new TransportInterface, or nullptr if type unsupported
 *
 * Note: Caller is responsible for memory management (delete).
 */
TransportInterface* createTransport(TransportType type);
