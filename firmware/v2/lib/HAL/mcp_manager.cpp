#include <Arduino.h>
#include "mcp_manager.h"
#include "../HAL/i2c_mutex.h"

void IRAM_ATTR MCPManager::_isr() {
    MCPManager::getInstance()._intFlag = true;
}

bool MCPManager::init() {
    if (_ready) {
        Serial.println("[MCP] Already initialized");
        return true;
    }
    _chipCount = 0;
    _ready = false;

    I2C_LOCK();
    for (int i = 0; i < MCP_MAX_CHIPS; i++) {
        uint8_t addr = MCP_CHIP_ADDR_BASE + i;
        if (_chips[i].begin_I2C(addr)) {
            _present[i] = true;
            _chipCount++;
            Serial.printf("[MCP] Chip %d found at 0x%02X\n", i, addr);
        } else {
            _present[i] = false;
        }
    }
    I2C_UNLOCK();

    if (_chipCount == 0) {
        Serial.println("[MCP] No chips found — expander disabled");
        return false;
    }

    // Attach interrupt if configured and not on V4/colliding hardware
    if (PIN_MCP_INT != -1) {
        pinMode(PIN_MCP_INT, INPUT_PULLUP);
#ifndef ARDUINO_HELTEC_WIFI_LORA_32_V4
        attachInterrupt(digitalPinToInterrupt(PIN_MCP_INT), _isr, FALLING);
#else
        Serial.println("[MCP] V4 Conflict: Pin 38 used by GPS. Interrupt disabled.");
#endif
        Serial.printf("[MCP] Interrupt attached on GPIO %d\n", PIN_MCP_INT);
    } else {
        Serial.println("[MCP] Interrupt disabled (V4/Conflict guard)");
    }

    _ready = true;
    Serial.printf("[MCP] Ready — %d chip(s) online\n", _chipCount);
    return true;
}

void MCPManager::mcpPinMode(int extPin, uint8_t mode) {
    int c = chipIndex(extPin);
    int p = pinIndex(extPin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !_present[c]) return;
    I2C_LOCK();
    _chips[c].pinMode(p, mode);
    I2C_UNLOCK();
}

void MCPManager::mcpDigitalWrite(int extPin, bool val) {
    int c = chipIndex(extPin);
    int p = pinIndex(extPin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !_present[c]) return;
    I2C_LOCK();
    _chips[c].digitalWrite(p, val ? HIGH : LOW);
    I2C_UNLOCK();
}

bool MCPManager::mcpDigitalRead(int extPin) {
    int c = chipIndex(extPin);
    int p = pinIndex(extPin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !_present[c]) return false;
    I2C_LOCK();
    bool val = _chips[c].digitalRead(p);
    I2C_UNLOCK();
    return val;
}

bool MCPManager::writePin(int pin, bool val) {
    if (!isMcpPin(pin)) {
        digitalWrite(pin, val ? HIGH : LOW);
        return true;
    }
    MCPManager &mgr = getInstance();
    int c = chipIndex(pin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !mgr._present[c]) {
        return false;
    }
    mgr.mcpDigitalWrite(pin, val);
    return true;
}

bool MCPManager::readPin(int pin) {
    if (!isMcpPin(pin)) {
        return digitalRead(pin);
    }
    MCPManager &mgr = getInstance();
    int c = chipIndex(pin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !mgr._present[c]) {
        return false;
    }
    return mgr.mcpDigitalRead(pin);
}

void MCPManager::setupPin(int pin, uint8_t mode) {
    if (!isMcpPin(pin)) {
        pinMode(pin, mode);
        return;
    }
    MCPManager &mgr = getInstance();
    int c = chipIndex(pin);
    if (c < 0 || c >= MCP_MAX_CHIPS || !mgr._present[c]) {
        return;
    }
    mgr.mcpPinMode(pin, mode);
}
