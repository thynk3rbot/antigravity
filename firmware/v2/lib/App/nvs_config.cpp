#include "nvs_config.h"
#include <WiFi.h>
#include <esp_efuse.h>

// ============================================================================
// Static member definitions
// ============================================================================

bool NVSConfig::_initialized = false;

// ============================================================================
// Initialization
// ============================================================================

bool NVSConfig::begin() {
    if (_initialized) return true;

    // Verify the namespace is accessible by opening and closing it
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.printf("[NVSConfig] ERROR: Failed to open NVS namespace '%s'\n",
                      PREFERENCES_NAMESPACE);
        return false;
    }

    _initialized = true;

    // Increment boot count
    uint32_t count = prefs.getUInt(NVS_KEY_BOOT_COUNT, 0);
    prefs.putUInt(NVS_KEY_BOOT_COUNT, count + 1);

    // Get reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    String reasonStr;
    switch (reason) {
        case ESP_RST_POWERON: reasonStr = "Power On"; break;
        case ESP_RST_EXT:     reasonStr = "External Pin"; break;
        case ESP_RST_SW:      reasonStr = "Software"; break;
        case ESP_RST_PANIC:   reasonStr = "Panic"; break;
        case ESP_RST_INT_WDT: reasonStr = "Internal WDT"; break;
        case ESP_RST_TASK_WDT: reasonStr = "Task WDT"; break;
        case ESP_RST_WDT:      reasonStr = "Other WDT"; break;
        case ESP_RST_DEEPSLEEP: reasonStr = "Deep Sleep"; break;
        case ESP_RST_BROWNOUT: reasonStr = "Brownout"; break;
        case ESP_RST_SDIO:     reasonStr = "SDIO"; break;
        default:               reasonStr = "Unknown"; break;
    }
    prefs.putString(NVS_KEY_RESET_REASON, reasonStr);
    
    prefs.end();

    Serial.printf("[NVSConfig] Initialized (namespace: '%s', Boot: %u, Reason: %s)\n", 
                  PREFERENCES_NAMESPACE, count + 1, reasonStr.c_str());
    return true;
}

// ============================================================================
// Factory Reset
// ============================================================================

void NVSConfig::factoryReset() {
    Serial.println("[NVSConfig] Factory reset: erasing all keys...");
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open namespace for factory reset");
        return;
    }
    prefs.clear();
    prefs.end();
    _initialized = false;
    Serial.println("[NVSConfig] Factory reset complete");
}

// ============================================================================
// Node Identity
// ============================================================================

String NVSConfig::_generateNodeId() {
    // Use ESP efuse base MAC — works before WiFi.begin(), unlike WiFi.macAddress()
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return "NODE_" + String(suffix);
}

static bool _isStaleNodeId(const String& id) {
    return id.length() == 0
        || id.equalsIgnoreCase("unknown")
        || id.equalsIgnoreCase("node")
        || id.equalsIgnoreCase("default")
        || id.equalsIgnoreCase("unnamed")
        || id.equalsIgnoreCase("unamed");
}

String NVSConfig::getNodeId() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        // Namespace open failed — return generated ID without persisting
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getNodeId");
        return _generateNodeId();
    }

    String id = prefs.getString(NVS_KEY_NODE_ID, "");
    prefs.end();

    if (_isStaleNodeId(id)) {
        id = _generateNodeId();
        Serial.printf("[NVSConfig] Node ID was '%s', generated: %s\n",
                      id.length() ? id.c_str() : "(empty)", id.c_str());
        setNodeId(id);
    }

    return id;
}

bool NVSConfig::setNodeId(const String& id) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setNodeId");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_NODE_ID, id);
    prefs.end();
    if (!ok) {
        Serial.printf("[NVSConfig] ERROR: Failed to write node_id\n");
        return false;
    }
    Serial.printf("[NVSConfig] Node ID set: %s\n", id.c_str());
    return true;
}

// ============================================================================
// WiFi Credentials
// ============================================================================

String NVSConfig::getWifiSSID() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getWifiSSID");
        return "";
    }
    String ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    prefs.end();
    return ssid;
}

String NVSConfig::getWifiPassword() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getWifiPassword");
        return "";
    }
    String pass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();
    return pass;
}

bool NVSConfig::setWifiCredentials(const String& ssid, const String& pass) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setWifiCredentials");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_WIFI_SSID, ssid) &&
              prefs.putString(NVS_KEY_WIFI_PASS, pass);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write WiFi credentials");
        return false;
    }
    Serial.printf("[NVSConfig] WiFi credentials set (SSID: %s)\n", ssid.c_str());
    return true;
}

// ============================================================================
// Static IP Configuration
// ============================================================================

String NVSConfig::getStaticIP() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getStaticIP");
        return "";
    }
    String ip = prefs.getString(NVS_KEY_STATIC_IP, "");
    prefs.end();
    return ip;
}

bool NVSConfig::setStaticIP(const String& ip) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setStaticIP");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_STATIC_IP, ip);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write static_ip");
        return false;
    }
    Serial.printf("[NVSConfig] Static IP set: %s\n", ip.c_str());
    return true;
}

String NVSConfig::getGateway() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getGateway");
        return "";
    }
    String gw = prefs.getString(NVS_KEY_GATEWAY, "");
    prefs.end();
    return gw;
}

bool NVSConfig::setGateway(const String& gw) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setGateway");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_GATEWAY, gw);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write gateway");
        return false;
    }
    Serial.printf("[NVSConfig] Gateway set: %s\n", gw.c_str());
    return true;
}

String NVSConfig::getSubnet() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getSubnet");
        return "";
    }
    String sn = prefs.getString(NVS_KEY_SUBNET, "");
    prefs.end();
    return sn;
}

bool NVSConfig::setSubnet(const String& sn) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setSubnet");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_SUBNET, sn);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write subnet");
        return false;
    }
    Serial.printf("[NVSConfig] Subnet set: %s\n", sn.c_str());
    return true;
}

// ============================================================================
// Crypto Key
// ============================================================================

String NVSConfig::getCryptoKey() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getCryptoKey");
        return "0102030405060708090A0B0C0D0E0F10";
    }
    String key = prefs.getString(NVS_KEY_CRYPTO_KEY, "");
    prefs.end();

    if (key.length() == 0) {
        Serial.println("[NVSConfig] Crypto key not set, returning default");
        return "0102030405060708090A0B0C0D0E0F10";
    }
    return key;
}

bool NVSConfig::setCryptoKey(const String& hexKey) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setCryptoKey");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_CRYPTO_KEY, hexKey);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write crypto key");
        return false;
    }
    Serial.println("[NVSConfig] Crypto key set");
    return true;
}

// ============================================================================
// Relay States
// ============================================================================

bool NVSConfig::getRelayState(uint8_t relayNum) {
    const char* key = (relayNum == 1) ? NVS_KEY_RELAY1 : NVS_KEY_RELAY2;
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.printf("[NVSConfig] ERROR: Failed to open NVS for getRelayState(%u)\n",
                      relayNum);
        return false;
    }
    bool state = prefs.getBool(key, false);
    prefs.end();
    return state;
}

bool NVSConfig::setRelayState(uint8_t relayNum, bool state) {
    if (relayNum != 1 && relayNum != 2) {
        Serial.printf("[NVSConfig] ERROR: Invalid relay number %u (must be 1 or 2)\n",
                      relayNum);
        return false;
    }
    const char* key = (relayNum == 1) ? NVS_KEY_RELAY1 : NVS_KEY_RELAY2;
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.printf("[NVSConfig] ERROR: Failed to open NVS for setRelayState(%u)\n",
                      relayNum);
        return false;
    }
    bool ok = prefs.putBool(key, state);
    prefs.end();
    if (!ok) {
        Serial.printf("[NVSConfig] ERROR: Failed to write relay%u state\n", relayNum);
        return false;
    }
    Serial.printf("[NVSConfig] Relay %u state set: %s\n", relayNum,
                  state ? "ON" : "OFF");
    return true;
}

// ============================================================================
// Power Mode
// ============================================================================

uint8_t NVSConfig::getPowerMode() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getPowerMode");
        return 0;
    }
    uint8_t mode = prefs.getUChar(NVS_KEY_POWER_MODE, 0);
    prefs.end();
    return mode;
}

bool NVSConfig::setPowerMode(uint8_t mode) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setPowerMode");
        return false;
    }
    bool ok = prefs.putUChar(NVS_KEY_POWER_MODE, mode);
    prefs.end();
    if (!ok) {
        Serial.printf("[NVSConfig] ERROR: Failed to write power_mode\n");
        return false;
    }
    Serial.printf("[NVSConfig] Power mode set: %u\n", mode);
    return true;
}

// ============================================================================
// MQTT Configuration
// ============================================================================

String NVSConfig::getMqttBroker() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getMqttBroker");
        return "";
    }
    String broker = prefs.getString(NVS_KEY_MQTT_BROKER, "");
    prefs.end();
    return broker;
}

bool NVSConfig::setMqttBroker(const String& broker) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setMqttBroker");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_MQTT_BROKER, broker);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write mqtt_broker");
        return false;
    }
    Serial.printf("[NVSConfig] MQTT broker set: %s\n", broker.c_str());
    return true;
}

uint16_t NVSConfig::getMqttPort() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getMqttPort");
        return 1883;
    }
    uint16_t port = prefs.getUShort(NVS_KEY_MQTT_PORT, 1883);
    prefs.end();
    return port;
}

bool NVSConfig::setMqttPort(uint16_t port) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setMqttPort");
        return false;
    }
    bool ok = prefs.putUShort(NVS_KEY_MQTT_PORT, port);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write mqtt_port");
        return false;
    }
    Serial.printf("[NVSConfig] MQTT port set: %u\n", port);
    return true;
}

String NVSConfig::getMqttUsername() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getMqttUsername");
        return "";
    }
    String user = prefs.getString(NVS_KEY_MQTT_USER, "");
    prefs.end();
    return user;
}

String NVSConfig::getMqttPassword() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getMqttPassword");
        return "";
    }
    String pass = prefs.getString(NVS_KEY_MQTT_PASS, "");
    prefs.end();
    return pass;
}

bool NVSConfig::setMqttCredentials(const String& user, const String& pass) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for setMqttCredentials");
        return false;
    }
    bool ok = prefs.putString(NVS_KEY_MQTT_USER, user) &&
              prefs.putString(NVS_KEY_MQTT_PASS, pass);
    prefs.end();
    if (!ok) {
        Serial.println("[NVSConfig] ERROR: Failed to write MQTT credentials");
        return false;
    }
    Serial.printf("[NVSConfig] MQTT credentials set (user: %s)\n", user.c_str());
    return true;
}

// ============================================================================
// Diagnostics
// ============================================================================

uint32_t NVSConfig::getBootCount() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        return 0;
    }
    uint32_t count = prefs.getUInt(NVS_KEY_BOOT_COUNT, 0);
    prefs.end();
    return count;
}

void NVSConfig::incrementBootCount() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        return;
    }
    uint32_t count = prefs.getUInt(NVS_KEY_BOOT_COUNT, 0);
    prefs.putUInt(NVS_KEY_BOOT_COUNT, count + 1);
    prefs.end();
}

String NVSConfig::getResetReason() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) {
        return "Unknown";
    }
    String reason = prefs.getString(NVS_KEY_RESET_REASON, "Unknown");
    prefs.end();
    return reason;
}

void NVSConfig::setResetReason(const String& reason) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) {
        return;
    }
    prefs.putString(NVS_KEY_RESET_REASON, reason);
    prefs.end();
}

// ============================================================================
// Hardware & Product Management
// ============================================================================

uint8_t NVSConfig::getHardwareVariant() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) return 4; // Default to V4
    uint8_t var = prefs.getUChar(NVS_KEY_HW_VER, 4);
    prefs.end();
    return var;
}

bool NVSConfig::setHardwareVariant(uint8_t version) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) return false;
    bool ok = prefs.putUChar(NVS_KEY_HW_VER, version);
    prefs.end();
    return ok;
}

String NVSConfig::getActiveProductName() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, true)) return "";
    String prod = prefs.getString(NVS_KEY_ACTIVE_PROD, "");
    prefs.end();
    return prod;
}

bool NVSConfig::setActiveProductName(const String& name) {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE, false)) return false;
    bool ok = prefs.putString(NVS_KEY_ACTIVE_PROD, name);
    prefs.end();
    return ok;
}
