/**
 * @file relay_hal.cpp
 * @brief Relay HAL Implementation
 */

#include "relay_hal.h"
#include "mcp_hal.h"
#include "native_hal.h"
#include <Arduino.h>
#include <cstring>
#include "../App/nvs_manager.h"

// Static instance
RelayHAL& relayHAL = RelayHAL::getInstance();

// Internal pin mapping (from board_config.h)
static const uint8_t g_relayPins[8] = {
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
  uint8_t initialized = 0;
  uint8_t skipped = 0;

  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    uint8_t pin = g_relayPins[i];
    
    if (pin < 255) {
      uPinMode(pin, OUTPUT);
      uDigitalWrite(pin, LOW); // Start all OFF
      initialized++;
    } else {
      skipped++;
    }
  }

  _state = 0x00;
  _enabled = 0xFF;
  _stateChangeCount = 0;

  // Restore saved relay state from NVS
  uint8_t savedMask = NVSManager::getRelayMask(0x00);
  if (savedMask != 0x00) {
    _applyState(savedMask);
    _state = savedMask;
    Serial.printf("[RelayHAL] Restored relay state 0x%02X from NVS\n", savedMask);
  }

  Serial.printf("[RelayHAL] Initialized %d relay channels (%d skipped) via Universal Routing\n",
                initialized, skipped);
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
  NVSManager::setRelayMask(_state);

  Serial.printf("[RelayHAL] State changed to 0x%02X (change #%lu)\n",
                _state, (unsigned long)_stateChangeCount);
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

  Serial.printf("[RelayHAL] Channel %d -> %s (change #%lu)\n", 
                channel, on ? "ON" : "OFF", (unsigned long)_stateChangeCount);
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
           getActiveCount(), MAX_RELAY_CHANNELS, _state, (unsigned long)_stateChangeCount);
  return buffer;
}

// ============================================================================
// Private Helpers
// ============================================================================

void RelayHAL::_applyState(uint8_t newState) {
  for (int i = 0; i < MAX_RELAY_CHANNELS; i++) {
    uint8_t pin = g_relayPins[i];
    if (pin < 255) {
      bool shouldBeOn = (newState & (1 << i)) != 0;
      uDigitalWrite(pin, shouldBeOn);
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
