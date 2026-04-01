/**
 * @file lora_transport.cpp
 * @brief LoRa Transport Implementation
 */

#include "lora_transport.h"
#include "crypto_hal.h"
#include <cstring>
#include <Arduino.h>

// Static instance
LoRaTransport& loraTransport = LoRaTransport::getInstance();

// ============================================================================
// Singleton Implementation
// ============================================================================

LoRaTransport& LoRaTransport::getInstance() {
  static LoRaTransport instance;
  return instance;
}

// ============================================================================
// Initialization
// ============================================================================

bool LoRaTransport::init() {
  if (_initialized) {
    return true;
  }

  // Initialize radio HAL
  if (!radioHAL.init()) {
    _lastError = static_cast<int>(TransportStatus::INIT_ERROR);
    return false;
  }

  // Initialize deduplication buffer
  memset(_dedupHashes, 0, sizeof(_dedupHashes));
  _dedupIndex = 0;

  // Default encryption key (16 bytes = 32 hex chars)
  const char* defaultKey = "MagicDefault!";
  setEncryptionKey(defaultKey);

  _encryptEnabled = true;
  _initialized = true;
  _txBytes = 0;
  _rxBytes = 0;

  Serial.println("[LoRaTransport] Initialized");
  return true;
}

void LoRaTransport::shutdown() {
  _initialized = false;
  Serial.println("[LoRaTransport] Shutdown");
}

bool LoRaTransport::isReady() const {
  return _initialized && radioHAL.selfTest();
}

// ============================================================================
// Send/Receive
// ============================================================================

int LoRaTransport::send(const uint8_t* payload, size_t len) {
  if (!_initialized || !payload) {
    _lastError = static_cast<int>(TransportStatus::NOT_READY);
    return _lastError;
  }

  // Copy to temp buffer for encryption
  uint8_t txBuffer[256];
  if (len > sizeof(txBuffer) - 20) {  // Reserve space for IV + tag
    _lastError = static_cast<int>(TransportStatus::BUFFER_FULL);
    return _lastError;
  }

  memcpy(txBuffer, payload, len);
  size_t encLen = len;

  // Encrypt if enabled
  if (_encryptEnabled && !_encryptPacket(txBuffer, &encLen)) {
    _lastError = static_cast<int>(TransportStatus::INVALID_ARG);
    return _lastError;
  }

  // Transmit via radio HAL
  int bytesSent = radioHAL.transmit(txBuffer, encLen, 0);  // Async TX
  if (bytesSent > 0) {
    _txBytes += bytesSent;
    Serial.printf("[LoRaTransport] TX: %d bytes (payload %zu)\n", bytesSent, len);
    return bytesSent;
  }

  _lastError = bytesSent;
  return _lastError;
}

int LoRaTransport::recv(uint8_t* buffer, size_t maxLen) {
  if (!_initialized || !buffer) {
    _lastError = static_cast<int>(TransportStatus::NOT_READY);
    return _lastError;
  }

  uint8_t rxBuffer[256];
  int len = radioHAL.receive(rxBuffer, sizeof(rxBuffer), 100);  // 100ms timeout

  if (len <= 0) {
    if (len < 0) {
      _lastError = len;
    }
    return len;
  }

  _lastRSSI = radioHAL.getLastRSSI();
  _lastSNR = radioHAL.getLastSNR();

  // Decrypt if enabled
  size_t decLen = len;
  if (_encryptEnabled && !_decryptPacket(rxBuffer, &decLen)) {
    _rxCrcErrors++;
    _lastError = static_cast<int>(TransportStatus::CRC_ERROR);
    return _lastError;
  }

  // Check for duplicates
  uint32_t hash = (rxBuffer[0] << 24) | (rxBuffer[1] << 16) |
                  (rxBuffer[2] << 8) | rxBuffer[3];
  if (isDuplicate(hash)) {
    _rxDuplicates++;
    Serial.printf("[LoRaTransport] Duplicate packet (hash 0x%08lx)\n", hash);
    return 0;  // Silently drop
  }

  // Copy to output buffer
  if (decLen > maxLen) {
    decLen = maxLen;
  }

  memcpy(buffer, rxBuffer, decLen);
  _rxBytes += decLen;

  Serial.printf("[LoRaTransport] RX: %zu bytes, RSSI=%d dBm, SNR=%.1f dB\n",
                decLen, _lastRSSI, _lastSNR / 4.0f);

  return decLen;
}

bool LoRaTransport::isAvailable() const {
  if (!_initialized) {
    return false;
  }
  return radioHAL.isRxAvailable();
}

void LoRaTransport::poll() {
  // Poll radio for any pending RX (non-blocking)
  if (isAvailable()) {
    uint8_t buffer[256];
    recv(buffer, sizeof(buffer));
  }
}

const char* LoRaTransport::getStatus() const {
  if (!_initialized) {
    return "NOT_READY";
  }
  if (isAvailable()) {
    return "RX_PENDING";
  }
  return "READY";
}

int8_t LoRaTransport::getSignalStrength() const {
  return _lastRSSI;
}

int8_t LoRaTransport::getLastSNR() const {
  return _lastSNR;
}

const char* LoRaTransport::getLastErrorString() const {
  switch (_lastError) {
    case 0: return "No error";
    case -1: return "TX in progress";
    case -2: return "RX timeout";
    case -3: return "CRC error";
    case -4: return "Invalid frequency";
    case -100: return "Init error";
    default: return "Unknown error";
  }
}

// ============================================================================
// Encryption & Deduplication
// ============================================================================

bool LoRaTransport::setEncryptionKey(const char* keyHex) {
  if (!keyHex || strlen(keyHex) < 32) {
    return false;
  }

  // Parse hex string to bytes
  for (int i = 0; i < 16; i++) {
    char hex[3];
    hex[0] = keyHex[i * 2];
    hex[1] = keyHex[i * 2 + 1];
    hex[2] = '\0';
    _encryptionKey[i] = strtol(hex, nullptr, 16);
  }

  Serial.println("[LoRaTransport] Encryption key set");
  return true;
}

bool LoRaTransport::isDuplicate(uint32_t packetHash) {
  for (int i = 0; i < LORA_DEDUP_SIZE; i++) {
    if (_dedupHashes[i] == packetHash) {
      return true;
    }
  }

  // Add to buffer
  _dedupHashes[_dedupIndex] = packetHash;
  _dedupIndex = (_dedupIndex + 1) % LORA_DEDUP_SIZE;

  return false;
}

// ============================================================================
// Encryption Stubs (Placeholder)
// ============================================================================

bool LoRaTransport::_encryptPacket(uint8_t* plaintext, size_t* len) {
  if (!plaintext || !len) return false;
  
  uint8_t temp[256];
  if (!cryptoHAL.encrypt(plaintext, *len, _encryptionKey, temp)) {
    return false;
  }
  
  *len = *len + CRYPTO_OVERHEAD;
  memcpy(plaintext, temp, *len);
  return true;
}

bool LoRaTransport::_decryptPacket(uint8_t* ciphertext, size_t* len) {
  if (!ciphertext || !len || *len <= CRYPTO_OVERHEAD) return false;
  
  uint8_t temp[256];
  if (!cryptoHAL.decrypt(ciphertext, *len, _encryptionKey, temp)) {
    return false;
  }
  
  *len = *len - CRYPTO_OVERHEAD;
  memcpy(ciphertext, temp, *len);
  return true;
}
