#include "nvs_config.h"
#include <WiFi.h>

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
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.printf("[NVSConfig] ERROR: Failed to open NVS namespace '%s'\n",
                      NVS_NAMESPACE);
        return false;
    }
    prefs.end();

    _initialized = true;
    Serial.printf("[NVSConfig] Initialized (namespace: '%s')\n", NVS_NAMESPACE);
    return true;
}

// ============================================================================
// Factory Reset
// ============================================================================

void NVSConfig::factoryReset() {
    Serial.println("[NVSConfig] Factory reset: erasing all keys...");
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
    // Generate from last 5 characters of MAC (e.g. "AA:BB:CC:DD:EE:FF" -> "DDEEFF")
    // WiFi.macAddress() returns "AA:BB:CC:DD:EE:FF" (17 chars)
    // substring(9) -> "DD:EE:FF", then strip colons
    String mac = WiFi.macAddress();
    String suffix = mac.substring(9); // "DD:EE:FF"
    suffix.replace(":", "");          // "DDEEFF"
    suffix.toUpperCase();
    return "NODE_" + suffix;
}

String NVSConfig::getNodeId() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getNodeId");
        return _generateNodeId();
    }

    String id = prefs.getString(NVS_KEY_NODE_ID, "");
    prefs.end();

    if (id.length() == 0) {
        // Not set yet — generate from MAC and persist it
        id = _generateNodeId();
        Serial.printf("[NVSConfig] Node ID not found, generated: %s\n", id.c_str());
        setNodeId(id);
    }

    return id;
}

bool NVSConfig::setNodeId(const String& id) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getWifiSSID");
        return "";
    }
    String ssid = prefs.getString(NVS_KEY_WIFI_SSID, "");
    prefs.end();
    return ssid;
}

String NVSConfig::getWifiPassword() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getWifiPassword");
        return "";
    }
    String pass = prefs.getString(NVS_KEY_WIFI_PASS, "");
    prefs.end();
    return pass;
}

bool NVSConfig::setWifiCredentials(const String& ssid, const String& pass) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
// Crypto Key
// ============================================================================

String NVSConfig::getCryptoKey() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
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
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
    if (!prefs.begin(NVS_NAMESPACE, true)) {
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
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("[NVSConfig] ERROR: Failed to open NVS for getPowerMode");
        return 0;
    }
    uint8_t mode = prefs.getUChar(NVS_KEY_POWER_MODE, 0);
    prefs.end();
    return mode;
}

bool NVSConfig::setPowerMode(uint8_t mode) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
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
