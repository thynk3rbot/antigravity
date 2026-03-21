/**
 * @file sensor_hal.cpp
 * @brief Implementation of modular Sensor HAL
 */

#include "sensor_hal.h"

SensorHAL& sensorHAL = SensorHAL::getInstance();

SensorHAL& SensorHAL::getInstance() {
    static SensorHAL instance;
    return instance;
}

void SensorHAL::registerPlugin(SensorPlugin* plugin) {
    if (plugin) {
        _plugins.push_back(plugin);
    }
}

void SensorHAL::init() {
    Serial.println("[SensorHAL] Initializing plugins...");
    auto it = _plugins.begin();
    while (it != _plugins.end()) {
        if ((*it)->init()) {
            Serial.printf("[SensorHAL] Plugin '%s' ready\n", (*it)->getName());
            ++it;
        } else {
            Serial.printf("[SensorHAL] Plugin '%s' failed to init - removing\n", (*it)->getName());
            it = _plugins.erase(it);
        }
    }
}

std::vector<SensorData> SensorHAL::readAll() {
    std::vector<SensorData> results;
    for (auto* plugin : _plugins) {
        SensorData data;
        if (plugin->read(data)) {
            results.push_back(data);
        }
    }
    return results;
}
