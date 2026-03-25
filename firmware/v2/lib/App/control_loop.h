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
};
