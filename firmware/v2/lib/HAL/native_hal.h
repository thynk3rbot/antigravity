/**
 * @file native_hal.h
 * @brief Native ESP32 implementations of HAL interfaces
 */

#pragma once

#include "interfaces.h"

/**
 * @class NativeDigitalIO
 * @brief Logic for native ESP32 GPIO pins
 */
class NativeDigitalIO : public IDigitalIO {
public:
    NativeDigitalIO(uint8_t pin) : _pin(pin) {}
    
    void mode(uint8_t m) override {
        if (_pin < 255) pinMode(_pin, m);
    }
    
    void write(bool level) override {
        if (_pin < 255) digitalWrite(_pin, level ? HIGH : LOW);
    }
    
    bool read() override {
        if (_pin < 255) return digitalRead(_pin) == HIGH;
        return false;
    }

private:
    uint8_t _pin;
};

/**
 * @class NativeAnalogIn
 * @brief Logic for native ESP32 ADC pins
 */
class NativeAnalogIn : public IAnalogIn {
public:
    NativeAnalogIn(uint8_t pin) : _pin(pin) {}
    
    uint16_t readRaw() override {
        if (_pin < 255) return analogRead(_pin);
        return 0;
    }
    
    float readVoltage() override {
        if (_pin < 255) {
            // Very basic 12-bit ADC to 3.3V mapping
            // In a real app, this would use calibration data
            return (analogRead(_pin) * 3.3f) / 4095.0f;
        }
        return 0.0f;
    }

private:
    uint8_t _pin;
};
