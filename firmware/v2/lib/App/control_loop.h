/**
 * @file control_loop.h
 * @brief Logic for periodic system maintenance, telemetry, and discovery
 */

#pragma once

#include <Arduino.h>

class ControlLoop {
public:
    static void execute(void* param);

private:
    static void updatePower();
    static void updateTelemetry();
    static void updateOLED();
    static void updateStatusRegistry();
    static void updateMesh();
    static void runDiscoveryBeacons();
    static void pollPlugins();

    // Cached sensor data to avoid duplicate reads (read once per 1s in updateOLED)
    static uint16_t cachedTempC_x10;
};
