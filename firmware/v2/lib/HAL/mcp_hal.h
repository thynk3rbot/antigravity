/**
 * @file mcp_hal.h
 * @brief Basic hooks for MCP23017 I2C Expander
 */

#pragma once

#include <stdint.h>
#include <Arduino.h>
#include <Adafruit_MCP23X17.h>

/**
 * @class MCPHAL
 * @brief Singleton for basic MCP23017 routing.
 * 
 * Provides simple hooks for digital read/write to expander pins.
 * Virtual pins 100-115 map to Chip 0, 116-131 to Chip 1, etc.
 */
class MCPHAL {
public:
    static MCPHAL& getInstance();

    /**
     * @brief Initialize expander chips
     * Scans for chips at addresses 0x20-0x27.
     */
    void init();

    /**
     * @brief Digital write to a virtual pin
     * @param virtualPin 100+ 
     * @param value HIGH/LOW
     */
    void digitalWrite(uint8_t virtualPin, uint8_t value);

    /**
     * @brief Digital read from a virtual pin
     * @param virtualPin 100+
     * @return HIGH/LOW
     */
    uint8_t digitalRead(uint8_t virtualPin);

    /**
     * @brief Configure pin mode
     * @param virtualPin 100+
     * @param mode INPUT/OUTPUT/etc
     */
    void pinMode(uint8_t virtualPin, uint8_t mode);

    /**
     * @brief Helper to check if pin is virtual
     */
    static bool isVirtual(uint8_t pin) { return pin >= 100; }

private:
    MCPHAL() = default;
    
    Adafruit_MCP23X17 _chips[8];
    bool _present[8] = {false};
    
    uint8_t _getChipIndex(uint8_t virtualPin) { return (virtualPin - 100) / 16; }
    uint8_t _getPinIndex(uint8_t virtualPin) { return (virtualPin - 100) % 16; }
};

extern MCPHAL& mcpHAL;
