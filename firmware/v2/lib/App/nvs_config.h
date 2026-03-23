#pragma once
#include <Arduino.h>
#include <Preferences.h>

#define PREFERENCES_NAMESPACE  "loralink"
#define NVS_KEY_NODE_ID    "dev_name"
#define NVS_KEY_WIFI_SSID  "wifi_ssid"
#define NVS_KEY_WIFI_PASS  "wifi_pass"
#define NVS_KEY_CRYPTO_KEY "crypto_key"
#define NVS_KEY_RELAY1     "relay1_state"
#define NVS_KEY_RELAY2     "relay2_state"
#define NVS_KEY_POWER_MODE "power_mode"
#define NVS_KEY_MQTT_BROKER "mqtt_broker"
#define NVS_KEY_MQTT_PORT   "mqtt_port"
#define NVS_KEY_MQTT_USER   "mqtt_user"
#define NVS_KEY_MQTT_PASS   "mqtt_pass"
#define NVS_KEY_BOOT_COUNT  "boot_count"
#define NVS_KEY_RESET_REASON "reset_reason"
#define NVS_KEY_STATIC_IP    "static_ip"
#define NVS_KEY_GATEWAY      "gateway"
#define NVS_KEY_SUBNET       "subnet"
#define NVS_KEY_HW_VER       "hw_ver"
#define NVS_KEY_ACTIVE_PROD   "active_prod"

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

    // Static IP configuration
    static String getStaticIP();
    static bool setStaticIP(const String& ip);
    static String getGateway();
    static bool setGateway(const String& gw);
    static String getSubnet();
    static bool setSubnet(const String& sn);

    // Crypto key (16 bytes, stored as hex string)
    static String getCryptoKey();
    static bool setCryptoKey(const String& hexKey);

    // Relay states (persisted across reboots)
    static bool getRelayState(uint8_t relayNum);  // relayNum 1 or 2
    static bool setRelayState(uint8_t relayNum, bool state);

    // Power mode (0=NORMAL, 1=CONSERVE, 2=CRITICAL)
    static uint8_t getPowerMode();
    static bool setPowerMode(uint8_t mode);

    // MQTT broker configuration
    static String getMqttBroker();
    static bool setMqttBroker(const String& broker);
    static uint16_t getMqttPort();
    static bool setMqttPort(uint16_t port);
    static String getMqttUsername();
    static String getMqttPassword();
    static bool setMqttCredentials(const String& user, const String& pass);

    // Diagnostics
    static uint32_t getBootCount();
    static void incrementBootCount();
    static String getResetReason();
    static void setResetReason(const String& reason);

    // Hardware & Product Management (V1 Parity)
    static uint8_t getHardwareVariant();
    static bool setHardwareVariant(uint8_t version);
    static String getActiveProductName();
    static bool setActiveProductName(const String& name);

private:
    static bool _initialized;
    static String _generateNodeId(); // Generate from MAC address
};
