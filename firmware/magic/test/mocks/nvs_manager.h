#pragma once
#include <string>

class NVSManager {
public:
    static std::string getNodeID(const char*) { return "TestNode"; }
    static bool setNodeID(const char*) { return true; }
    static bool setNodeID(const char*, const char*) { return true; }
    static std::string getWiFiSSID() { return "MockSSID"; }
    static bool setWiFiSSID(const char*) { return true; }
    static bool setWiFiPassword(const char*) { return true; }
    static std::string getStaticIP() { return ""; }
    static bool setStaticIP(const char*) { return true; }
    static std::string getGateway() { return ""; }
    static bool setGateway(const char*) { return true; }
    static std::string getSubnet() { return ""; }
    static bool setSubnet(const char*) { return true; }
    static std::string getHardwareVersion(const char* d) { return d; }
    static bool setRelayState(uint8_t, bool) { return true; }
    static bool getRelayState(uint8_t) { return false; }
    static int getPowerMode(int d) { return d; }
    static bool setCryptoKey(uint8_t*) { return true; }
    static void clearAll() {}
};
