/**
 * @file radio_hal.h
 * @brief Hardware Abstraction Layer for LoRa Radio (SX1276 vs SX1262)
 *
 * Encapsulates RadioLib to handle both SX1276 (V2) and SX1262 (V3/V4).
 * Provides async RX/TX with encryption, packet deduplication, and diagnostics.
 */

#pragma once

#include <RadioLib.h>
#include "board_config.h"
#include <stdint.h>
#include <cstddef>

// ============================================================================
// Radio Status Codes
// ============================================================================

enum class RadioStatus : int {
  OK                = 0,
  TX_IN_PROGRESS    = -1,
  RX_TIMEOUT        = -2,
  CRC_ERROR         = -3,
  INVALID_FREQUENCY = -4,
  INVALID_BANDWIDTH = -5,
  INIT_ERROR        = -100,
};

// ============================================================================
// Radio HAL Class
// ============================================================================

/**
 * @class RadioHAL
 * @brief Abstract hardware layer for LoRa radio control
 *
 * Handles:
 * - SPI initialization and RadioLib module setup
 * - Frequency/bandwidth/SF configuration
 * - Async TX with interrupt callbacks
 * - RX with timeout and RSSI reporting
 * - Encryption/decryption (moved from radio to transport layer)
 * - Spectrum scanning & diagnostics
 */
class RadioHAL {
public:
  /**
   * @brief Initialize LoRa radio
   * @return true on success, false on failure
   */
  bool init();

  /**
   * @brief Transmit raw LoRa packet (non-blocking)
   * @param payload Pointer to data buffer
   * @param len Length in bytes (max 255 for most configs)
   * @param timeoutMs Max time to wait for TX to complete (0 = async fire-and-forget)
   * @return Number of bytes transmitted, or negative RadioStatus code
   */
  int transmit(const uint8_t* payload, size_t len, uint16_t timeoutMs = 0);

  /**
   * @brief Receive LoRa packet (blocking with timeout)
   * @param buffer Pointer to output buffer
   * @param maxLen Maximum bytes to read
   * @param timeoutMs Receive timeout in milliseconds (0 = non-blocking)
   * @return Number of bytes received, 0 if timeout, or negative RadioStatus code
   */
  int receive(uint8_t* buffer, size_t maxLen, uint16_t timeoutMs = 100);

  /**
   * @brief Check if RX data is available (non-blocking)
   * @return true if packet ready, false otherwise
   */
  bool isRxAvailable() const;

  /**
   * @brief Get RSSI (Received Signal Strength Indicator) of last packet
   * @return RSSI in dBm (negative value, e.g., -80 dBm)
   */
  int8_t getLastRSSI() const;

  /**
   * @brief Get SNR (Signal-to-Noise Ratio) of last packet
   * @return SNR in dB (can be negative)
   */
  int8_t getLastSNR() const;

  /**
   * @brief Get frequency error offset
   * @return Frequency error in Hz
   */
  int32_t getFreqError() const;

  // ========================================================================
  // Configuration Methods
  // ========================================================================

  /**
   * @brief Set LoRa frequency
   * @param freqMHz Frequency in MHz (e.g., 915.0)
   */
  void setFrequency(float freqMHz);

  /**
   * @brief Set LoRa bandwidth
   * @param bwKHz Bandwidth in kHz (125, 250, 500)
   */
  void setBandwidth(float bwKHz);

  /**
   * @brief Set spreading factor (SF)
   * @param sf Spreading factor (7-12, higher = longer range, slower)
   */
  void setSpreadingFactor(uint8_t sf);

  /**
   * @brief Set transmit power
   * @param powerDbm Power in dBm (2-20, board-dependent max)
   */
  void setTxPower(int8_t powerDbm);

  /**
   * @brief Enable/disable CRC check
   * @param enable true = enable, false = disable
   */
  void setCRC(bool enable);

  /**
   * @brief Set preamble length
   * @param len Preamble length in symbols (short = 6, long = 12+)
   *            Short = faster but less robust; Long = slower but more range
   */
  void setPreambleLength(uint8_t len);

  // ========================================================================
  // Diagnostics & Testing
  // ========================================================================

  /**
   * @brief Perform radio self-test
   * @return true if self-test passes
   */
  bool selfTest();

  /**
   * @brief Get current radio state
   * @return Human-readable state string
   */
  const char* getRadioState() const;

  /**
   * @brief Perform spectrum scan (CAD)
   * @return true if channel is clear, false if activity detected
   */
  bool isChannelClear();

    /**
     * @brief Set LoRa preamble length
     * @param length Number of symbols (e.g. 8 for standard, 512 for CAD wake-up)
     * @return True if successful
     */
    bool setPreambleLength(uint16_t length);

  /**
   * @brief Get last TX time-on-air
   * @return Duration in milliseconds
   */
  uint16_t getLastTxDuration() const;

  /**
   * @brief Get cumulative TX time (since boot)
   * @return Total duration in seconds
   */
  uint32_t getCumulativeTxTime() const;

  // ========================================================================
  // Singleton Access
  // ========================================================================

  static RadioHAL& getInstance();

  /**
   * @brief Set task to notify on packet reception
   * @param task Task handle to notify
   */
  void setNotifyTask(TaskHandle_t task);

private:
  // Static ISR handler for RadioLib
  #if defined(RADIO_SX1276) || defined(RADIO_SX1262)
  static void setFlag();
  #endif
  // Private constructor for singleton
  RadioHAL();

  // RadioLib module pointers (abstracted)
  #ifdef RADIO_SX1276
    Module* _spiModule = nullptr;
    SX1276* _radio = nullptr;
  #elif defined(RADIO_SX1262)
    Module* _spiModule = nullptr;
    SX1262* _radio = nullptr;
  #endif

  // State tracking
  bool _initialized = false;
  int8_t _lastRSSI = 0;
  int8_t _lastSNR = 0;
  uint32_t _cumulativeTxMs = 0;
  uint16_t _lastTxDurationMs = 0;
  TaskHandle_t _notifyTask = nullptr;
  static volatile bool _receivedFlag;

  // RX buffer (local temporary)
  uint8_t _rxBuffer[256];
  size_t _rxLen = 0;

  // Private initialization
  bool _initSX1276();
  bool _initSX1262();
};

// ============================================================================
// Global Instance Access
// ============================================================================

extern RadioHAL& radioHAL;

// Convenience inline function
inline RadioHAL& getRadioHAL() {
  return RadioHAL::getInstance();
}
