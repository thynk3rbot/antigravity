/**
 * @file mcp_hal.cpp
 * @brief Implementation of basic MCP23017 hooks.
 */

#include "mcp_hal.h"
#include "board_config.h"

MCPHAL& mcpHAL = MCPHAL::getInstance();

MCPHAL& MCPHAL::getInstance() {
    static MCPHAL instance;
    return instance;
}

void MCPHAL::init() {
    Serial.println("[MCPHAL] Scanning for I2C expanders...");
    for (int i = 0; i < 8; i++) {
        uint8_t addr = 0x20 + i;
        if (_chips[i].begin_I2C(addr)) {
            _present[i] = true;
            Serial.printf("[MCPHAL] Found MCP23017 at 0x%02X (Chip %d)\n", addr, i);
        }
    }
}

void MCPHAL::digitalWrite(uint8_t virtualPin, uint8_t value) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        _chips[c].digitalWrite(p, value);
    }
}

uint8_t MCPHAL::digitalRead(uint8_t virtualPin) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        return _chips[c].digitalRead(p);
    }
    return LOW;
}

void MCPHAL::pinMode(uint8_t virtualPin, uint8_t mode) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        _chips[c].pinMode(p, mode);
    }
}
