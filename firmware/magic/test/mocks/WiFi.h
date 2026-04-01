#pragma once
#include "../arduino_stubs.h"

class WiFiClass {
public:
    struct IP { String toString() { return "192.168.1.100"; } };
    String macAddress() { return "AA:BB:CC:DD:EE:FF"; }
    IP localIP() { return IP(); }
    int status() { return 3; } // WL_CONNECTED
    int RSSI() { return -60; }
};

extern WiFiClass WiFi;
