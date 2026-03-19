#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define NVS_NAMESPACE  "loralink"
#define NVS_KEY_NODE_ID    "node_id"
#define NVS_KEY_WIFI_SSID  "wifi_ssid"
#define NVS_KEY_WIFI_PASS  "wifi_pass"
#define NVS_KEY_CRYPTO_KEY "crypto_key"
#define NVS_KEY_RELAY1     "relay1_state"
#define NVS_KEY_RELAY2     "relay2_state"
#define NVS_KEY_POWER_MODE "power_mode"

class NVSConfig {
public:
    static bool begin();       // Initialize NVS, returns false on error
    static void factoryReset(); // Erase all keys in namespace

    // Node identity
    static String getNodeId();                    // Returns stored ID or auto-generates from MAC
    static bool setNodeId(const String& id);

    // WiFi credentials
    static String getWifiSSID();
    static String getWifiPassword();
    static bool setWifiCredentials(const String& ssid, const String& pass);

    // Crypto key (16 bytes, stored as hex string)
    static String getCryptoKey();
    static bool setCryptoKey(const String& hexKey);

    // Relay states (persisted across reboots)
    static bool getRelayState(uint8_t relayNum);  // relayNum 1 or 2
    static bool setRelayState(uint8_t relayNum, bool state);

    // Power mode (0=NORMAL, 1=CONSERVE, 2=CRITICAL)
    static uint8_t getPowerMode();
    static bool setPowerMode(uint8_t mode);

private:
    static bool _initialized;
    static String _generateNodeId(); // Generate from MAC address
};
