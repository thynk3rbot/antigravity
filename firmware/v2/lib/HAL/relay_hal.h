/**
 * @file relay_hal.h
 * @brief Relay Control HAL (GPIO-based relay management)
 *
 * Manages relay channels via GPIO pins (native or MCP23017 expander).
 * Supports up to 8 relay channels with bitmask state control.
 */

#pragma once

#include "board_config.h"
#include <stdint.h>

// ============================================================================
// Relay Channel Configuration
// ============================================================================

/**
 * @struct RelayConfig
 * @brief Configuration for a single relay channel
 */
struct RelayConfig {
  uint8_t channel;              // Channel index (0-7)
  uint8_t gpio;                 // GPIO pin number
  bool    inverted = false;     // true = active-LOW (default), false = active-HIGH
  bool    enabled = true;       // Channel enabled at startup
};

// ============================================================================
// Relay HAL Class
// ============================================================================

/**
 * @class RelayHAL
 * @brief Singleton for relay GPIO control
 *
 * Manages relay state via uint8_t bitmask:
 *   Bit N = Relay channel N (0-7)
 *   1 = ON, 0 = OFF
 *
 * All state changes are atomic and logged for diagnostics.
 */
class RelayHAL {
public:
  /**
   * @brief Initialize relay GPIO pins
   * Sets all pins to OUTPUT mode and turns relays OFF.
   */
  void init();

  /**
   * @brief Set relay state via bitmask
   * @param mask 8-bit bitmask (bit N controls relay N)
   *
   * Example:
   *   setState(0b0011) => Relays 0,1 ON; 2-7 OFF
   *   setState(0xFF)   => All relays ON
   *   setState(0x00)   => All relays OFF
   */
  void setState(uint8_t mask);

  /**
   * @brief Get current relay state
   * @return 8-bit bitmask of relay states
   */
  uint8_t getState() const;

  /**
   * @brief Toggle a single relay
   * @param channel Channel index (0-7)
   */
  void toggle(uint8_t channel);

  /**
   * @brief Set individual relay to ON or OFF
   * @param channel Channel index (0-7)
   * @param on true = ON, false = OFF
   */
  void setChannel(uint8_t channel, bool on);

  /**
   * @brief Get state of a single relay
   * @param channel Channel index (0-7)
   * @return true if ON, false if OFF
   */
  bool getChannel(uint8_t channel) const;

  /**
   * @brief Enable or disable a relay channel
   * When disabled, writes are ignored.
   * @param channel Channel index (0-7)
   * @param enabled true = enable, false = disable
   */
  void enableChannel(uint8_t channel, bool enabled);

  /**
   * @brief Check if a channel is enabled
   * @param channel Channel index (0-7)
   * @return true if enabled, false otherwise
   */
  bool isChannelEnabled(uint8_t channel) const;

  /**
   * @brief Emergency stop: turn all relays OFF
   */
  void emergencyStop();

  /**
   * @brief Get number of ON relays
   * @return Count of relays in ON state (0-8)
   */
  uint8_t getActiveCount() const;

  /**
   * @brief Get relay diagnostics string
   * @return Human-readable relay state (e.g., "Relays: 0 ON, 8 total")
   */
  const char* getDiagnostics() const;

  // ========================================================================
  // Singleton Access
  // ========================================================================

  static RelayHAL& getInstance();

private:
  // Private constructor for singleton
  RelayHAL() = default;

  // State tracking
  uint8_t _state = 0x00;                    // Current relay bitmask
  uint8_t _enabled = 0xFF;                  // Enabled channels bitmask
  uint32_t _stateChangeCount = 0;           // Diagnostic counter

  // GPIO mapping (from board_config.h)
  static const uint8_t _relayPins[8];

  // Private helper to apply GPIO change
  void _applyState(uint8_t newState);
};

// ============================================================================
// Global Instance Access
// ============================================================================

extern RelayHAL& relayHAL;

// Convenience inline function
inline RelayHAL& getRelayHAL() {
  return RelayHAL::getInstance();
}

// ============================================================================
// Relay Utility Functions (Optional convenience)
// ============================================================================

/**
 * @brief Convert relay names to bitmask
 * Example: relayNamesToMask("CH0,CH3,CH7") -> 0b10001001
 */
uint8_t relayNamesToMask(const char* names);

/**
 * @brief Convert bitmask to relay names
 * Example: maskToRelayNames(0b0011) -> "CH0,CH1"
 */
const char* maskToRelayNames(uint8_t mask);
