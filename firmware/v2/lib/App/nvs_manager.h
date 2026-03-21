/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) persistence layer for device configuration
 *
 * Provides persistent storage for device configuration including:
 * - Node ID, WiFi credentials, encryption keys
 * - MQTT broker settings, hardware version
 *
 * Uses ESP-IDF's built-in NVS API with "loralink" namespace.
 * All operations are thread-safe and include error checking.
 */

#pragma once

#include <string>
#include <cstdint>

/**
 * @class NVSManager
 * @brief Static manager for NVS (Non-Volatile Storage) operations
 *
 * Provides simple get/set methods for device configuration.
 * Includes graceful fallback to defaults for missing values.
 *
 * Thread-safe: NVS operations are protected by ESP-IDF's internal locking.
 * Each call opens/closes NVS handle (small overhead, acceptable for config reads).
 *
 * @note NVS namespace: "loralink" - all keys are grouped under this partition
 * @note Maximum string length in NVS: 4000 bytes
 */
class NVSManager {
public:
    /**
     * @brief Initialize NVS partition and open "loralink" namespace
     *
     * Must be called once during boot before any other NVS operations.
     * Initializes NVS flash partition if not already done.
     *
     * @return true if initialization successful, false on error
     */
    static bool init();

    // ========================================================================
    // Device Identity
    // ========================================================================

    static std::string getHardwareID();
    static bool setNodeID(const std::string& id);

    /**
     * @brief Get device node ID
     *
     * @param defaultVal Default value if not set in NVS
     * @return Node ID string or defaultVal if not found
     */
    static std::string getNodeID(const std::string& defaultVal = "Node");

    // ========================================================================
    // WiFi Configuration
    // ========================================================================

    /**
     * @brief Set WiFi SSID (network name)
     *
     * @param ssid WiFi network name (max 32 bytes)
     * @return true if set successfully, false on error
     */
    static bool setWiFiSSID(const std::string& ssid);

    /**
     * @brief Get WiFi SSID
     *
     * @param defaultVal Default value if not set
     * @return WiFi SSID or empty string if not configured
     */
    static std::string getWiFiSSID(const std::string& defaultVal = "");

    /**
     * @brief Set WiFi password
     *
     * @param pass WiFi password (max 64 bytes)
     * @return true if set successfully, false on error
     */
    static bool setWiFiPassword(const std::string& pass);

    /**
     * @brief Get WiFi password
     *
     * @param defaultVal Default value if not set
     * @return WiFi password or empty string if not configured
     */
    static std::string getWiFiPassword(const std::string& defaultVal = "");

    // ========================================================================
    // Encryption Configuration
    // ========================================================================

    /**
     * @brief Set AES-128 encryption key (16 bytes)
     *
     * @param key 16-byte binary key for AES-128 encryption
     * @return true if set successfully, false on error
     */
    static bool setCryptoKey(const uint8_t key[16]);

    /**
     * @brief Get AES-128 encryption key
     *
     * @param outKey Buffer for 16-byte key (caller must allocate)
     * @return true if key found and copied, false if not set or error
     */
    static bool getCryptoKey(uint8_t outKey[16]);

    /**
     * @brief Generate and store random AES-128 key
     *
     * Generates a random 16-byte key and stores in NVS.
     * Useful for initial device provisioning.
     *
     * @return true if generated and stored successfully, false on error
     */
    static bool generateAndStoreCryptoKey();

    // ========================================================================
    // MQTT Configuration
    // ========================================================================

    /**
     * @brief Set MQTT broker address
     *
     * @param broker Broker hostname or IP (max 64 bytes, e.g., "mqtt.example.com")
     * @return true if set successfully, false on error
     */
    static bool setMQTTBroker(const std::string& broker);

    /**
     * @brief Get MQTT broker address
     *
     * @param defaultVal Default value if not set
     * @return MQTT broker address or empty string if not configured
     */
    static std::string getMQTTBroker(const std::string& defaultVal = "");

    /**
     * @brief Set MQTT port number
     *
     * @param port Port number (typically 1883 for MQTT, 8883 for MQTT over TLS)
     * @return true if set successfully, false on error
     */
    static bool setMQTTPort(uint16_t port);

    /**
     * @brief Get MQTT port number
     *
     * @param defaultVal Default port if not set (typically 1883)
     * @return MQTT port number
     */
    static uint16_t getMQTTPort(uint16_t defaultVal = 1883);

    // ========================================================================
    // Hardware Version
    // ========================================================================

    /**
     * @brief Set hardware version string
     *
     * @param hwver Hardware version (max 8 bytes, e.g., "V2", "V3", "V4")
     * @return true if set successfully, false on error
     */
    static bool setHardwareVersion(const std::string& hwver);

    /**
     * @brief Get hardware version string
     *
     * @param defaultVal Default value if not set
     * @return Hardware version string
     */
    static std::string getHardwareVersion(const std::string& defaultVal = "V3");

    // ========================================================================
    // Utility Operations
    // ========================================================================

    /**
     * @brief Clear all NVS data in "loralink" namespace
     *
     * Erases all stored configuration.
     * Use with caution - this cannot be undone without reprogramming.
     *
     * @return true if cleared successfully, false on error
     */
    static bool clearAll();

    /**
     * @brief Print all stored configuration to Serial for diagnostics
     *
     * Useful for debugging and verifying NVS contents.
     * Outputs all stored values and their sizes.
     */
    static void printInfo();

private:
    // NVS namespace for this application
    static constexpr const char* NVS_NAMESPACE = "loralink";

    /**
     * @brief Internal helper to log errors with context
     *
     * @param operation Name of the operation (e.g., "setNodeID")
     * @param key NVS key name
     * @param err ESP error code
     */
    static void logError(const char* operation, const char* key, int err);
};
