/**
 * @file lora_transport.h
 * @brief LoRa Transport Implementation (SX1276 or SX1262)
 *
 * Extends TransportInterface for LoRa communication with:
 * - AES-128-GCM encryption/decryption
 * - Packet deduplication via rolling hash
 * - RSSI/SNR reporting
 * - Async RX/TX with timeout handling
 */

#pragma once

#include "interface.h"
#include "../HAL/radio_hal.h"
#include <cstring>

// ============================================================================
// LoRa Transport Configuration
// ============================================================================

#define LORA_RX_BUFFER_SIZE    256
#define LORA_ENCRYPT_ENABLED   1      // AES-128-GCM by default
#define LORA_DEDUP_SIZE        16     // Deduplication buffer (rolling hash)

// ============================================================================
// LoRa Transport Class
// ============================================================================

/**
 * @class LoRaTransport
 * @brief LoRa radio transport implementation
 *
 * Provides encrypted, deduped LoRa communication between nodes.
 * All packets are transparently encrypted (configurable key in NVS).
 */
class LoRaTransport : public TransportInterface {
public:
  /**
   * @brief Initialize LoRa radio and encryption
   * @return true on success
   */
  bool init() override;

  void shutdown() override;

  bool isReady() const override;

  /**
   * @brief Send encrypted LoRa packet
   * @param payload Raw data buffer
   * @param len Packet length (max 240 after encryption overhead)
   * @return Bytes sent, or negative TransportStatus code
   */
  int send(const uint8_t* payload, size_t len) override;

  /**
   * @brief Receive and decrypt LoRa packet
   * @param buffer Output buffer for decrypted data
   * @param maxLen Max bytes to read
   * @return Bytes received, 0 if timeout, or negative error code
   */
  int recv(uint8_t* buffer, size_t maxLen) override;

  bool isAvailable() const override;

  void poll() override;

  const char* getName() const override { return "LoRa"; }

  TransportType getType() const override { return TransportType::LORA; }

  const char* getStatus() const override;

  /**
   * @brief Get RSSI of last received packet
   * @return RSSI in dBm (negative, e.g., -80)
   */
  int8_t getSignalStrength() const override;

  /**
   * @brief Get SNR of last received packet
   * @return SNR in dB
   */
  int8_t getLastSNR() const;

  uint32_t getTxBytes() const override { return _txBytes; }

  uint32_t getRxBytes() const override { return _rxBytes; }

  int getLastError() const override { return _lastError; }

  void clearError() override { _lastError = 0; }

  const char* getLastErrorString() const override;

  // ========================================================================
  // LoRa-Specific Configuration
  // ========================================================================

  /**
   * @brief Set encryption key (hex string)
   * @param keyHex 32-character hex string (16 bytes AES key)
   * @return true on success
   */
  bool setEncryptionKey(const char* keyHex);

  /**
   * @brief Enable/disable encryption
   * @param enable true = encrypt (default), false = plaintext
   */
  void setEncryptionEnabled(bool enable) { _encryptEnabled = enable; }

  /**
   * @brief Check if a packet is a duplicate
   * @param packetHash Hash of packet (e.g., seq number)
   * @return true if seen before, false if new
   */
  bool isDuplicate(uint32_t packetHash);

  // ========================================================================
  // Singleton Access
  // ========================================================================

  static LoRaTransport& getInstance();

private:
  // Private constructor for singleton
  LoRaTransport() = default;

  // State
  bool _initialized = false;
  uint8_t _rxBuffer[LORA_RX_BUFFER_SIZE];
  size_t _rxLen = 0;
  int _lastError = 0;

  // Statistics
  uint32_t _txBytes = 0;
  uint32_t _rxBytes = 0;
  uint32_t _rxCrcErrors = 0;
  uint32_t _rxDuplicates = 0;

  // Encryption
  bool _encryptEnabled = true;
  uint8_t _encryptionKey[16];  // 128-bit AES key

  // Packet deduplication (rolling hash buffer)
  uint32_t _dedupHashes[LORA_DEDUP_SIZE];
  uint8_t _dedupIndex = 0;

  // Last packet metadata
  int8_t _lastRSSI = 0;
  int8_t _lastSNR = 0;

  // Encryption helpers
  bool _encryptPacket(uint8_t* plaintext, size_t* len);
  bool _decryptPacket(uint8_t* ciphertext, size_t* len);
};

// ============================================================================
// Global Instance Access
// ============================================================================

extern LoRaTransport& loraTransport;

inline LoRaTransport& getLoRaTransport() {
  return LoRaTransport::getInstance();
}
