/**
 * @file radio_hal.cpp
 * @brief LoRa Radio HAL Implementation
 */

#include "radio_hal.h"
#include <Arduino.h>

// Static instance
RadioHAL& radioHAL = RadioHAL::getInstance();

// Static ISR members
volatile bool RadioHAL::_receivedFlag = false;

#if defined(RADIO_SX1276) || defined(RADIO_SX1262)
void RadioHAL::setFlag() {
  _receivedFlag = true;
  RadioHAL& instance = RadioHAL::getInstance();
  if (instance._notifyTask) {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(instance._notifyTask, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
  }
}
#endif

// ============================================================================
// Constructor & Singleton Implementation
// ============================================================================

RadioHAL::RadioHAL() {
  // Defer radio initialization to init()
}

RadioHAL& RadioHAL::getInstance() {
  static RadioHAL instance;
  return instance;
}

// ============================================================================
// Initialization
// ============================================================================

bool RadioHAL::init() {
  if (_initialized) {
    return true;
  }

  #ifdef RADIO_SX1276
    return _initSX1276();
  #elif defined(RADIO_SX1262)
    return _initSX1262();
  #else
    return false;
  #endif
}

bool RadioHAL::_initSX1276() {
  #ifdef RADIO_SX1276
  // Initialize SPI module
  _spiModule = new Module(LORA_CS, LORA_DIO0, LORA_RESET, LORA_DIO1);
  if (!_spiModule) {
    return false;
  }

  // Allocate and initialize SX1276
  _radio = new SX1276(_spiModule);
  if (!_radio) {
    return false;
  }

  int state = _radio->begin(LORA_FREQ_MHZ, 125.0, 10, 5, 0x12, 14, 8);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[RadioHAL] SX1276 init failed: %d\n", state);
    return false;
  }

  // Set TX power (Match V1: 14 dBm)
  _radio->setOutputPower(14);

  // Set interrupt callback
  _radio->setPacketReceivedAction(setFlag);

  // Start RX
  _radio->startReceive();

  _initialized = true;
  Serial.println("[RadioHAL] SX1276 initialized");
  return true;
  #endif
}

bool RadioHAL::_initSX1262() {
#ifdef RADIO_SX1262
  // Initialize SPI module
  _spiModule = new Module(LORA_CS, LORA_DIO1, LORA_RESET, LORA_BUSY);
  if (!_spiModule) {
    return false;
  }

  // Allocate and initialize SX1262
  _radio = new SX1262(_spiModule);
  if (!_radio) {
    return false;
  }

  // SX126x: Freq, BW, SF, CR, Sync, Pwr, Preamble, Tcxo, LDO
  // CRITICAL: Heltec V3/V4 MUST have 1.6V TCXO and LDO=false for stability.
  int state = _radio->begin(LORA_FREQ_MHZ, 125.0, 10, 5, 0x12, 14, 8, 1.6f, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[RadioHAL] SX1262 init failed: %d\n", state);
    return false;
  }

  // Set TX power (Start at 14dBm, match V1 stability profile)
  _radio->setOutputPower(14);

  // Set interrupt callback
  _radio->setPacketReceivedAction(setFlag);

  // Start RX
  _radio->startReceive();

  _initialized = true;
  Serial.println("[RadioHAL] SX1262 initialized");
  return true;
#else
  return false;
#endif
}

// ============================================================================
// RX/TX Operations
// ============================================================================

int RadioHAL::transmit(const uint8_t* payload, size_t len, uint16_t timeoutMs) {
  if (!_initialized || !payload || len == 0 || len > 255) {
    return static_cast<int>(RadioStatus::INVALID_FREQUENCY);
  }

  uint32_t start = millis();
  int state = _radio->transmit((uint8_t*)payload, len);

  if (timeoutMs > 0) {
    while (millis() - start < timeoutMs) {
      // TODO: Check if transmission is done using RadioLib API
      _lastTxDurationMs = millis() - start;
      _cumulativeTxMs += _lastTxDurationMs;
      return len;
    }
    return static_cast<int>(RadioStatus::RX_TIMEOUT);
  }

  if (state == RADIOLIB_ERR_NONE) {
    _lastTxDurationMs = millis() - start;
    _cumulativeTxMs += _lastTxDurationMs;
    return len;
  }

  return state;
}

int RadioHAL::receive(uint8_t* buffer, size_t maxLen, uint16_t timeoutMs) {
  if (!_initialized || !buffer || maxLen == 0) {
    return static_cast<int>(RadioStatus::INVALID_FREQUENCY);
  }

  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    // Check if data is available and read it
    int state = _radio->receive(buffer, maxLen);
    if (state == RADIOLIB_ERR_NONE) {
      // Received data successfully
      _lastRSSI = _radio->getRSSI();
      _lastSNR = _radio->getSNR();
      return maxLen;  // For now, return maxLen (TODO: get actual length)
    }
    if (state != RADIOLIB_ERR_RX_TIMEOUT) {
      return state;  // Non-timeout error
    }
    delay(10);
  }

  return static_cast<int>(RadioStatus::RX_TIMEOUT);
}

bool RadioHAL::isRxAvailable() const {
  if (!_initialized) {
    return false;
  }
  // TODO: Implement using RadioLib's available/status methods
  return false;
}

int8_t RadioHAL::getLastRSSI() const {
  return _lastRSSI;
}

int8_t RadioHAL::getLastSNR() const {
  return _lastSNR;
}

int32_t RadioHAL::getFreqError() const {
  // TODO: Get frequency error from RadioLib if available
  return 0;
}

// ============================================================================
// Configuration
// ============================================================================

void RadioHAL::setFrequency(float freqMHz) {
  if (_initialized) {
  _radio->setFrequency(freqMHz);
  }
}

void RadioHAL::setBandwidth(float bwKHz) {
  if (_initialized) {
  _radio->setBandwidth(bwKHz);
  }
}

void RadioHAL::setSpreadingFactor(uint8_t sf) {
  if (_initialized && sf >= 7 && sf <= 12) {
  _radio->setSpreadingFactor(sf);
  }
}

void RadioHAL::setTxPower(int8_t powerDbm) {
  if (_initialized) {
  _radio->setOutputPower(powerDbm);
  }
}

void RadioHAL::setCRC(bool enable) {
  if (_initialized) {
  _radio->setCRC(enable);
  }
}

void RadioHAL::setPreambleLength(uint8_t len) {
  if (_initialized) {
  _radio->setPreambleLength(len);
  }
}

// ============================================================================
// Diagnostics
// ============================================================================

bool RadioHAL::selfTest() {
  if (!_initialized) {
    return false;
  }
  // TODO: Implement self-test using RadioLib
  return true;
}

const char* RadioHAL::getRadioState() const {
  if (!_initialized) {
    return "NOT_INIT";
  }
  // Simplified; RadioLib has radio->getOpMode()
  return "ACTIVE";
}

bool RadioHAL::setPreambleLength(uint16_t length) {
  if (!_initialized) return false;
  int state = _radio->setPreambleLength(length);
  return (state == RADIOLIB_ERR_NONE);
}

bool RadioHAL::isChannelClear() {
  if (!_initialized) {
    return false;
  }
  
  // RadioLib CAD Implementation
  int state = _radio->scanChannel();
  
  if (state == RADIOLIB_LORA_DETECTED) {
    return false; // Activity detected
  }
  
  return true; // Channel is clear
}

uint16_t RadioHAL::getLastTxDuration() const {
  return _lastTxDurationMs;
}

uint32_t RadioHAL::getCumulativeTxTime() const {
  return _cumulativeTxMs / 1000;  // Convert to seconds
}

void RadioHAL::setNotifyTask(TaskHandle_t task) {
  _notifyTask = task;
}
