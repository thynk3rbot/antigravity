/**
 * @file nvs_manager.cpp
 * @brief NVS persistence module implementation
 *
 * Uses ESP32 Preferences API (built-in, no external dependency) to store
 * device configuration in NVS (flash). Thread-safe for this use case since
 * configuration is only modified during setup/provisioning.
 */

#include "nvs_manager.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <Preferences.h>
#endif

#ifdef NATIVE_TEST
#include <map>
#include <vector>
#include <cstring>
#endif

// ============================================================================
// Static state and initialization
// ============================================================================

#ifdef NATIVE_TEST
// Mock Preferences for unit testing
struct MockPreferences {
    std::map<String, uint8_t> uchars;
    std::map<String, String> strings;
    std::map<String, std::vector<uint8_t>> bytes_storage;

    bool begin(const char* name, bool read_only) {
        (void)name;
        (void)read_only;
        return true;
    }

    uint8_t getUChar(const char* key, uint8_t default_value) {
        String k(key);
        auto it = uchars.find(k);
        return (it != uchars.end()) ? it->second : default_value;
    }

    void putUChar(const char* key, uint8_t value) {
        String k(key);
        uchars[k] = value;
    }

    String getString(const char* key, const String& default_value) {
        String k(key);
        auto it = strings.find(k);
        return (it != strings.end()) ? it->second : default_value;
    }

    void putString(const char* key, const String& value) {
        String k(key);
        strings[k] = value;
    }

    size_t getBytes(const char* key, void* buf, size_t len) {
        String k(key);
        auto it = bytes_storage.find(k);
        if (it == bytes_storage.end()) return 0;

        const auto& stored = it->second;
        if (stored.size() != len) return 0;

        std::memcpy(buf, stored.data(), len);
        return len;
    }

    void putBytes(const char* key, const void* buf, size_t len) {
        String k(key);
        const uint8_t* data = static_cast<const uint8_t*>(buf);
        bytes_storage[k] = std::vector<uint8_t>(data, data + len);
    }

    void clear() {
        uchars.clear();
        strings.clear();
        bytes_storage.clear();
    }
};

static MockPreferences _prefs;
#else
static Preferences _prefs;
#endif

static bool _initialized = false;

// ============================================================================
// NVSManager implementation
// ============================================================================

bool NVSManager::init() {
    if (_initialized) return true;

    bool ok = _prefs.begin("loralink", false);  // "loralink" namespace, RW mode
    _initialized = ok;
    return ok;
}

uint8_t NVSManager::getDeviceID() {
    if (!_initialized) return 0;
    return _prefs.getUChar("device_id", 0);
}

bool NVSManager::setDeviceID(uint8_t id) {
    if (!_initialized) return false;
    if (id < 1 || id > 254) return false;  // 0 and 255 reserved

    _prefs.putUChar("device_id", id);
    return true;
}

String NVSManager::getWifiSSID() {
    if (!_initialized) return String("");
    return _prefs.getString("wifi_ssid", "");
}

bool NVSManager::setWifiSSID(const char* ssid) {
    if (!_initialized) return false;
    if (!ssid) return false;

    // Check length limit (WiFi SSID max is 32 bytes)
    size_t len = 0;
    for (const char* p = ssid; *p; p++) len++;
    if (len > 32) return false;

    _prefs.putString("wifi_ssid", ssid);
    return true;
}

String NVSManager::getWifiPassword() {
    if (!_initialized) return String("");
    return _prefs.getString("wifi_pass", "");
}

bool NVSManager::setWifiPassword(const char* pass) {
    if (!_initialized) return false;
    if (!pass) return false;

    // Check length limit (WiFi password max is 64 bytes)
    size_t len = 0;
    for (const char* p = pass; *p; p++) len++;
    if (len > 64) return false;

    _prefs.putString("wifi_pass", pass);
    return true;
}

bool NVSManager::getCryptoKey(uint8_t* key_out) {
    if (!_initialized || !key_out) return false;

    size_t len = _prefs.getBytes("crypto_key", key_out, 16);
    return len == 16;
}

bool NVSManager::setCryptoKey(const uint8_t* key) {
    if (!_initialized || !key) return false;

    _prefs.putBytes("crypto_key", key, 16);
    return true;
}

bool NVSManager::clear() {
    if (!_initialized) return false;
    _prefs.clear();
    return true;
}

bool NVSManager::isConfigured() {
    return getDeviceID() != 0;
}

#ifdef NATIVE_TEST
// Mock String implementation for native tests
String::String(const char* s) {
    if (!s) {
        _data = nullptr;
        return;
    }
    int len = 0;
    for (const char* p = s; *p; p++) len++;
    _data = new char[len + 1];
    for (int i = 0; i <= len; i++) _data[i] = s[i];
}

String::~String() {
    if (_data) delete[] _data;
}

int String::length() const {
    if (!_data) return 0;
    int len = 0;
    for (const char* p = _data; *p; p++) len++;
    return len;
}

bool String::operator==(const String& other) const {
    const char* a = _data ? _data : "";
    const char* b = other._data ? other._data : "";
    for (; *a && *b; a++, b++) {
        if (*a != *b) return false;
    }
    return *a == *b;  // Both empty
}

bool String::operator==(const char* s) const {
    const char* a = _data ? _data : "";
    const char* b = s ? s : "";
    for (; *a && *b; a++, b++) {
        if (*a != *b) return false;
    }
    return *a == *b;
}

String& String::operator=(const char* s) {
    if (_data) delete[] _data;
    if (!s) {
        _data = nullptr;
        return *this;
    }
    int len = 0;
    for (const char* p = s; *p; p++) len++;
    _data = new char[len + 1];
    for (int i = 0; i <= len; i++) _data[i] = s[i];
    return *this;
}
#endif
