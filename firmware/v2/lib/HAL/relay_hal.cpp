/**
 * @file relay_hal.cpp
 * @brief Relay HAL Implementation
 */

#include "relay_hal.h"
#include "mcp_hal.h"
#include <Arduino.h>
#include <cstring>

// Static instance
RelayHAL& relayHAL = RelayHAL::getInstance();

// Static relay pin mapping
const uint8_t RelayHAL::_relayPins[8] = {
  RELAY_CH0, RELAY_CH1, RELAY_CH2, RELAY_CH3,
  RELAY_CH4, RELAY_CH5, RELAY_CH6, RELAY_CH7
};

// ============================================================================
// Singleton Implementation
// ============================================================================

RelayHAL& RelayHAL::getInstance() {
  static RelayHAL instance;
  return instance;
}

// ============================================================================
// Initialization
// ============================================================================

void RelayHAL::init() {
  // Set all relay pins as OUTPUT
  // Note: On V3/V4 (SX1262), GPIO 12/13 are tied to LORA_RESET/LORA_BUSY.
  // board_config.h sets RELAY_CHx = 255 (sentinel) to prevent GPIO conflicts.
  // The check 'if (pin < 255)' skips these reserved pins.
  uint8_t skipped = 0;
  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    uint8_t pin = _relayPins[i];
    if (MCPHAL::isVirtual(pin)) {
      mcpHAL.pinMode(pin, OUTPUT);
      mcpHAL.digitalWrite(pin, LOW);
    } else if (pin < 255) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);  // Start all OFF (active-LOW)
    } else {
      skipped++;
    }
  }

  _state = 0x00;
  _enabled = 0xFF;  // All enabled
  _stateChangeCount = 0;

  Serial.printf("[RelayHAL] Initialized %d relay channels (%d skipped - reserved pins)\n",
                MAX_RELAY_CHANNELS, skipped);
}

// ============================================================================
// State Management
// ============================================================================

void RelayHAL::setState(uint8_t mask) {
  if (mask == _state) {
    return;  // No change
  }

  _applyState(mask);
  _state = mask;
  _stateChangeCount++;

  Serial.printf("[RelayHAL] State changed to 0x%02X (change #%lu)\n",
                _state, _stateChangeCount);
}

uint8_t RelayHAL::getState() const {
  return _state;
}

void RelayHAL::toggle(uint8_t channel) {
  if (channel >= MAX_RELAY_CHANNELS) {
    return;
  }

  if (_state & (1 << channel)) {
    setChannel(channel, false);  // Currently ON, turn OFF
  } else {
    setChannel(channel, true);   // Currently OFF, turn ON
  }
}

void RelayHAL::setChannel(uint8_t channel, bool on) {
  if (channel >= MAX_RELAY_CHANNELS || !(_enabled & (1 << channel))) {
    return;  // Channel out of range or disabled
  }

  if (on) {
    _state |= (1 << channel);   // Set bit
  } else {
    _state &= ~(1 << channel);  // Clear bit
  }

  _applyState(_state);
  _stateChangeCount++;

  Serial.printf("[RelayHAL] Channel %d -> %s\n", channel, on ? "ON" : "OFF");
}

bool RelayHAL::getChannel(uint8_t channel) const {
  if (channel >= MAX_RELAY_CHANNELS) {
    return false;
  }
  return (_state & (1 << channel)) != 0;
}

void RelayHAL::enableChannel(uint8_t channel, bool enabled) {
  if (channel >= MAX_RELAY_CHANNELS) {
    return;
  }

  if (enabled) {
    _enabled |= (1 << channel);
  } else {
    _enabled &= ~(1 << channel);
    // Force channel OFF when disabled
    _state &= ~(1 << channel);
  }

  Serial.printf("[RelayHAL] Channel %d -> %s\n",
                channel, enabled ? "ENABLED" : "DISABLED");
}

bool RelayHAL::isChannelEnabled(uint8_t channel) const {
  if (channel >= MAX_RELAY_CHANNELS) {
    return false;
  }
  return (_enabled & (1 << channel)) != 0;
}

void RelayHAL::emergencyStop() {
  Serial.println("[RelayHAL] EMERGENCY STOP - All relays OFF");
  _state = 0x00;
  _applyState(_state);
}

uint8_t RelayHAL::getActiveCount() const {
  uint8_t count = 0;
  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    if (_state & (1 << i)) {
      count++;
    }
  }
  return count;
}

const char* RelayHAL::getDiagnostics() const {
  static char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "Relays: %d ON, %d total (0x%02X) - %lu changes",
           getActiveCount(), MAX_RELAY_CHANNELS, _state, _stateChangeCount);
  return buffer;
}

// ============================================================================
// Private Helpers
// ============================================================================

void RelayHAL::_applyState(uint8_t newState) {
  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    uint8_t pin = _relayPins[i];
    // Skip pins with sentinel value 255 (reserved pins on V3/V4 SX1262 boards)
    if (MCPHAL::isVirtual(pin)) {
      bool shouldBeOn = (newState & (1 << i)) != 0;
      mcpHAL.digitalWrite(pin, shouldBeOn ? HIGH : LOW);
    } else if (pin < 255) {
      bool shouldBeOn = (newState & (1 << i)) != 0;
      // Relays are typically active-LOW, but this depends on hardware
      digitalWrite(pin, shouldBeOn ? HIGH : LOW);
    }
  }
}

// ============================================================================
// Utility Functions
// ============================================================================

uint8_t relayNamesToMask(const char* names) {
  if (!names) return 0;

  uint8_t mask = 0;
  char buffer[64];
  strncpy(buffer, names, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char* token = strtok(buffer, ",");
  while (token) {
    if (sscanf(token, "CH%hhu", &token[0]) == 1) {
      uint8_t ch = atoi(&token[2]);
      if (ch < MAX_RELAY_CHANNELS) {
        mask |= (1 << ch);
      }
    }
    token = strtok(nullptr, ",");
  }

  return mask;
}

const char* maskToRelayNames(uint8_t mask) {
  static char buffer[64];
  buffer[0] = '\0';

  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    if (mask & (1 << i)) {
      if (buffer[0] != '\0') {
        strcat(buffer, ",");
      }
      char temp[8];
      snprintf(temp, sizeof(temp), "CH%d", i);
      strcat(buffer, temp);
    }
  }

  return buffer;
}
