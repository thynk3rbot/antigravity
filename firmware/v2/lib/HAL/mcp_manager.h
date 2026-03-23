#ifndef MCP_MANAGER_H
#define MCP_MANAGER_H

#include "board_config.h"
#include <Adafruit_MCP23X17.h>
#include <Arduino.h>

/**
 * @class MCPManager
 * @brief MCP23017 I2C GPIO Expander Driver
 *
 * Restored from V1 industrial baseline. Supports up to 8 MCP23017 chips.
 * Extended pin numbering: native 0–99, MCP 100–227.
 */
class MCPManager {
public:
    static MCPManager &getInstance() {
        static MCPManager instance;
        return instance;
    }

    bool init();

    bool isReady() const { return _ready; }
    int chipCount() const { return _chipCount; }

    void mcpPinMode(int extPin, uint8_t mode);
    void mcpDigitalWrite(int extPin, bool val);
    bool mcpDigitalRead(int extPin);

    // Static routing helpers (transparently handles native vs MCP pins)
    static bool writePin(int pin, bool val);
    static bool readPin(int pin);
    static void setupPin(int pin, uint8_t mode);

    static bool isMcpPin(int pin) { return pin >= MCP_PIN_BASE; }
    static int chipIndex(int pin) { return (pin - MCP_PIN_BASE) / MCP_CHIP_PINS; }
    static int pinIndex(int pin) { return (pin - MCP_PIN_BASE) % MCP_CHIP_PINS; }

    bool hasInterrupt() const { return _intFlag; }
    void clearInterrupt() { _intFlag = false; }

    MCPManager(const MCPManager &) = delete;
    MCPManager &operator=(const MCPManager &) = delete;

private:
    MCPManager() = default;

    Adafruit_MCP23X17 _chips[MCP_MAX_CHIPS];
    bool _present[MCP_MAX_CHIPS] = {};
    int _chipCount = 0;
    bool _ready = false;
    volatile bool _intFlag = false;

    static void IRAM_ATTR _isr();
};

#endif // MCP_MANAGER_H
