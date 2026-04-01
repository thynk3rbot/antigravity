/**
 * @file dht_sensor.h
 * @brief DHT22 Sensor Plugin for SensorHAL
 */

#pragma once

#include "sensor_hal.h"
#include "interfaces.h"
#include "native_hal.h"
#include <DHT.h>

class DHTSensorPlugin : public SensorPlugin {
public:
    DHTSensorPlugin(IDigitalIO* io, uint8_t type = DHT22) 
        : _io(io), _type(type), _dht(0, type) {
        // Warning: The standard DHT library is hardcoded to native pins.
        // For now, we assume _io is a NativeDigitalIO and we'd extract its pin.
    }

    bool init() override {
        _dht.begin();
        // Check if sensor is responding
        float t = _dht.readTemperature();
        return !isnan(t);
    }

    bool read(SensorData& data) override {
        // This is a bit tricky since read() returns one data point
        // SensorHAL might need to support multiple data points per plugin
        // For now, we'll just return temperature
        float t = _dht.readTemperature();
        if (isnan(t)) return false;

        data.type = SensorType::TEMPERATURE;
        data.value = t;
        data.unit = "C";
        data.name = "DHT22 Temp";
        return true;
    }

    const char* getName() const override { return "DHT22"; }

private:
    IDigitalIO* _io;
    uint8_t _type;
    DHT _dht;
};
