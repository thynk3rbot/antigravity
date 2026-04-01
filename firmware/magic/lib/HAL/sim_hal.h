/**
 * @file sim_hal.h
 * @brief Simulated hardware abstraction for native simulation
 */

#pragma once

#include <stdint.h>
#include <map>
#include <cmath>
#include "hal_compat.h"

class SimDigitalIO {
public:
    SimDigitalIO(uint8_t pin) : _pin(pin), _val(0) {}
    void write(bool val) { _val = val; }
    bool read() { return _val; }
    uint8_t getPin() const { return _pin; }
private:
    uint8_t _pin;
    bool _val;
};

class SimAnalogIn {
public:
    SimAnalogIn(uint8_t pin) : _pin(pin), _val(0) {}
    uint32_t readRaw() {
        // Dummy sine wave for simulation
        _val = 2000 + 500 * sin(0.001 * millis());
        return _val;
    }
    uint8_t getPin() const { return _pin; }
private:
    uint8_t _pin;
    uint32_t _val;
};
