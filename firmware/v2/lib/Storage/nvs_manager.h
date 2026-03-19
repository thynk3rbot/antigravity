/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) manager for ESP32 persistent configuration
 *
 * Provides a clean interface for storing and retrieving device configuration:
 * - Device ID (node identifier, 1-254)
 * - WiFi SSID and password
 * - AES-128 encryption key
 *
 * Hides Preferences API details from the rest of the firmware.
 * All methods are static; call init() once before any get/set operations.
 */

#pragma once

#include <cstdint>

#ifdef NATIVE_TEST
// Forward declare String for native tests
class String {
public:
    String() : _data(nullptr) {}
    String(const char* s);
    ~String();
    const char* c_str() const { return _data; }
    int length() const;
    bool operator==(const String& other) const;
    bool operator==(const char* s) const;
    String& operator=(const char* s);

private:
    char* _data;
};
#else
#include <Arduino.h>  // For String on ESP32
#endif

class NVSManager {
public:
    /**
     * Initialize NVS. Must be called once before any get/set operations.
     * @return true if initialization succeeded, false otherwise
     */
    static bool init();

    /**
     * Get device ID (node identifier).
     * @return Device ID (1-254), or 0 if not set
     */
    static uint8_t getDeviceID();

    /**
     * Set device ID.
     * @param id Device ID (must be 1-254; 0 and 255 are reserved)
     * @return true if set succeeded, false if invalid ID or not initialized
     */
    static bool setDeviceID(uint8_t id);

    /**
     * Get WiFi SSID.
     * @return WiFi network name, or empty string if not set
     */
    static String getWifiSSID();

    /**
     * Set WiFi SSID.
     * @param ssid WiFi network name (max 32 chars)
     * @return true if set succeeded, false if SSID too long or not initialized
     */
    static bool setWifiSSID(const char* ssid);

    /**
     * Get WiFi password.
     * @return WiFi password, or empty string if not set
     */
    static String getWifiPassword();

    /**
     * Set WiFi password.
     * @param pass WiFi password (max 64 chars)
     * @return true if set succeeded, false if password too long or not initialized
     */
    static bool setWifiPassword(const char* pass);

    /**
     * Get AES-128 encryption key.
     * @param key_out Pointer to 16-byte buffer to receive key
     * @return true if key exists and was copied, false if not set or invalid buffer
     */
    static bool getCryptoKey(uint8_t* key_out);

    /**
     * Set AES-128 encryption key.
     * @param key Pointer to 16-byte encryption key
     * @return true if set succeeded, false if invalid pointer or not initialized
     */
    static bool setCryptoKey(const uint8_t* key);

    /**
     * Erase all stored configuration (factory reset).
     * @return true if clear succeeded, false if not initialized
     */
    static bool clear();

    /**
     * Check if device has been configured.
     * A device is considered configured if device ID has been set.
     * @return true if device ID is set (1-254), false otherwise
     */
    static bool isConfigured();
};
