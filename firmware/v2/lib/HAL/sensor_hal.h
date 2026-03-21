/**
 * @file sensor_hal.h
 * @brief Modular "Plugin" Sensor HAL
 */

#pragma once

#include <stdint.h>
#include <vector>
#include <functional>
#include <Arduino.h>

/**
 * @enum SensorType
 */
enum class SensorType {
    TEMPERATURE,
    HUMIDITY,
    PRESSURE,
    VOLTAGE,
    CURRENT,
    OTHER
};

/**
 * @struct SensorData
 */
struct SensorData {
    SensorType type;
    float value;
    const char* unit;
    const char* name;
};

/**
 * @class SensorPlugin
 * @brief Base class for sensor drivers
 */
class SensorPlugin {
public:
    virtual ~SensorPlugin() = default;
    virtual bool init() = 0;
    virtual bool read(SensorData& data) = 0;
    virtual const char* getName() const = 0;
};

/**
 * @class SensorHAL
 * @brief Manager for discovered sensors
 */
class SensorHAL {
public:
    static SensorHAL& getInstance();

    /**
     * @brief Register a new sensor plugin
     */
    void registerPlugin(SensorPlugin* plugin);

    /**
     * @brief Discover and initialize all registered plugins
     */
    void init();

    /**
     * @brief Read all sensors
     * @return List of sensor readings
     */
    std::vector<SensorData> readAll();

private:
    SensorHAL() = default;
    std::vector<SensorPlugin*> _plugins;
};

extern SensorHAL& sensorHAL;
