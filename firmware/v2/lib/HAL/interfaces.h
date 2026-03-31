/**
 * @file interfaces.h
 * @brief Abstract Hardware Interfaces for Magic HAL
 */

#pragma once

#include <stdint.h>
#include <vector>
#include "hal_compat.h"

/**
 * @class IDigitalIO
 * @brief Abstract interface for digital input/output
 */
class IDigitalIO {
public:
    virtual ~IDigitalIO() = default;
    
    /** @brief Set the pin mode (INPUT, OUTPUT, etc.) */
    virtual void mode(uint8_t m) = 0;
    
    /** @brief Write level to pin */
    virtual void write(bool level) = 0;
    
    /** @brief Read level from pin */
    virtual bool read() = 0;
};

/**
 * @class IAnalogIn
 * @brief Abstract interface for analog input (ADC)
 */
class IAnalogIn {
public:
    virtual ~IAnalogIn() = default;
    
    /** @brief Read raw ADC value */
    virtual uint16_t readRaw() = 0;
    
    /** @brief Read voltage (volts) */
    virtual float readVoltage() = 0;
};
