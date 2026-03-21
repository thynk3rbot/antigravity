#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>
#include "../HAL/board_config.h"

class GPSManager {
public:
    struct GPSData {
        double lat = 0.0;
        double lon = 0.0;
        double alt = 0.0;
        uint32_t fixAge = 0;
        uint32_t satellites = 0;
        bool hasFix = false;
        uint32_t hdop = 0;
    };

    static bool init();
    static void update();
    
    static GPSData getData();
    static String getStatusJSON();
    
    // Commands
    static String handleCommand(const String& args);

private:
    static TinyGPSPlus _gps;
    static GPSData _currentData;
    static uint32_t _lastUpdate;
    static bool _powerOn;

    static void _powerEnable(bool on);
};
