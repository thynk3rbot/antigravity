/**
 * @file mcp_hal.cpp
 * @brief Implementation of basic MCP23017 hooks.
 */

#include "mcp_hal.h"
#include "board_config.h"
#include "i2c_mutex.h"

MCPHAL& mcpHAL = MCPHAL::getInstance();

MCPHAL& MCPHAL::getInstance() {
    static MCPHAL instance;
    return instance;
}

void MCPHAL::init() {
    Serial.println("[MCPHAL] Scanning for I2C expanders...");
    I2C_LOCK();
    for (int i = 0; i < 8; i++) {
        uint8_t addr = 0x20 + i;
        if (_chips[i].begin_I2C(addr)) {
            _present[i] = true;
            Serial.printf("[MCPHAL] Found MCP23017 at 0x%02X (Chip %d)\n", addr, i);
        }
    }
    I2C_UNLOCK();
}

void MCPHAL::digitalWrite(uint8_t virtualPin, uint8_t value) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        I2C_LOCK();
        _chips[c].digitalWrite(p, value);
        I2C_UNLOCK();
    }
}

uint8_t MCPHAL::digitalRead(uint8_t virtualPin) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        I2C_LOCK();
        uint8_t val = _chips[c].digitalRead(p);
        I2C_UNLOCK();
        return val;
    }
    return LOW;
}

void MCPHAL::pinMode(uint8_t virtualPin, uint8_t mode) {
    uint8_t c = _getChipIndex(virtualPin);
    uint8_t p = _getPinIndex(virtualPin);
    if (c < 8 && _present[c]) {
        I2C_LOCK();
        _chips[c].pinMode(p, mode);
        I2C_UNLOCK();
    }
}
