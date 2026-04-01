/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) persistence layer for device configuration
 *
 * Provides persistent storage for device configuration including:
 * - Node ID, WiFi credentials, encryption keys
 * - MQTT broker settings, hardware version
 *
 * Uses ESP-IDF's built-in NVS API with "magic" namespace.
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
 * @note NVS namespace: "magic" - all keys are grouped under this partition
 * @note Maximum string length in NVS: 4000 bytes
 */
class NVSManager {
public:
    /**
     * @brief Initialize NVS partition and open "magic" namespace
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
    // Static IP & Network Configuration
    // ========================================================================
    static std::string getStaticIP(const std::string& defaultVal = "");
    static bool setStaticIP(const std::string& ip);
    static std::string getGateway(const std::string& defaultVal = "");
    static bool setGateway(const std::string& gw);
    static std::string getSubnet(const std::string& defaultVal = "");
    static bool setSubnet(const std::string& sn);

    // ========================================================================
    // Relay & Power Management
    // ========================================================================
    static bool getRelayState(uint8_t relayNum);
    static bool setRelayState(uint8_t relayNum, bool state);
    static uint8_t getRelayMask(uint8_t defaultVal = 0x00);
    static bool setRelayMask(uint8_t mask);
    static uint8_t getPowerMode(uint8_t defaultVal = 0);
    static bool setPowerMode(uint8_t mode);

    // ========================================================================
    // Product & Hardware Management
    // ========================================================================
    static uint8_t getHardwareVariant(uint8_t defaultVal = 4);
    static bool setHardwareVariant(uint8_t version);
    static std::string getActiveProductName(const std::string& defaultVal = "");
    static bool setActiveProductName(const std::string& name);

    // ========================================================================
    // Diagnostics & Boot Tracking
    // ========================================================================
    static uint32_t getBootCount();
    static void incrementBootCount();
    static std::string getResetReason();
    static void setResetReason(const std::string& reason);

    // ========================================================================
    // Security & Key Derivation (Phase 2)
    // ========================================================================
    static bool setNetworkSecret(const uint8_t secret[16]);
    static bool getDerivedKey(const uint8_t peerMAC[6], uint8_t outKey[16]);

    // ========================================================================
    // Modular Deployment Architecture (Phase 3)
    // ========================================================================

    /**
     * @brief Check if a firmware feature is enabled via NVS toggle
     * @param feature Name of the feature (e.g., "mqtt", "gps")
     * @param defaultVal Default if key is missing (usually true for unprovisioned)
     * @return true if enabled, false if explicitly disabled
     */
    static bool isFeatureEnabled(const std::string& feature, bool defaultVal = true);
    static bool setFeatureEnabled(const std::string& feature, bool enabled);

    /**
     * @brief Get a hardware configuration value from "hw" namespace
     */
    static int32_t getHardwareConfigInt(const std::string& key, int32_t defaultVal = -1);
    static std::string getHardwareConfigStr(const std::string& key, const std::string& defaultVal = "");
    static bool setHardwareConfigInt(const std::string& key, int32_t value);
    static bool setHardwareConfigStr(const std::string& key, const std::string& value);

    /**
     * @brief Get mesh/identity config from "mesh" namespace
     */
    static std::string getMeshConfigStr(const std::string& key, const std::string& defaultVal = "");
    static bool setMeshConfigStr(const std::string& key, const std::string& value);

    // ========================================================================
    // Utility Operations
    // ========================================================================
    static bool clearAll();
    static bool clearNamespace(const char* nsName);
    static void printInfo();
    
    // Generic internal accessors (Exposed for StatusBuilder/Plugins)
    static int32_t getInt32(const char* nsName, const char* key, int32_t defaultVal);
    static std::string getString(const char* nsName, const char* key, const std::string& defaultVal);

private:
    static constexpr const char* NVS_NAMESPACE     = "magic";
    static constexpr const char* NS_FEATURES       = "features";
    static constexpr const char* NS_HW             = "hw";
    static constexpr const char* NS_MESH           = "mesh";
    
    // Generic internal accessors
    static bool getBool(const char* nsName, const char* key, bool defaultVal);
    static bool setBool(const char* nsName, const char* key, bool value);
    static bool setInt32(const char* nsName, const char* key, int32_t value);
    static bool setString(const char* nsName, const char* key, const std::string& value);

    // Authoritative Keys (Aligned with legacy NVSConfig for zero-data-loss)
    static constexpr const char* KEY_NODE_ID     = "dev_name";
    static constexpr const char* KEY_WIFI_SSID   = "wifi_ssid";
    static constexpr const char* KEY_WIFI_PASS   = "wifi_pass";
    static constexpr const char* KEY_CRYPTO_KEY  = "crypto_key";
    static constexpr const char* KEY_RELAY1      = "relay1_state";
    static constexpr const char* KEY_RELAY2      = "relay2_state";
    static constexpr const char* KEY_RELAY_MASK  = "relay_mask";
    static constexpr const char* KEY_POWER_MODE  = "power_mode";
    static constexpr const char* KEY_MQTT_BROKER = "mqtt_broker";
    static constexpr const char* KEY_MQTT_PORT   = "mqtt_port";
    static constexpr const char* KEY_BOOT_COUNT  = "boot_count";
    static constexpr const char* KEY_RESET_REASON = "reset_reason";
    static constexpr const char* KEY_STATIC_IP   = "static_ip";
    static constexpr const char* KEY_GATEWAY     = "gateway";
    static constexpr const char* KEY_SUBNET      = "subnet";
    static constexpr const char* KEY_HW_VAR      = "hw_ver";
    static constexpr const char* KEY_ACTIVE_PROD = "active_prod";

    static void logError(const char* operation, const char* key, int err);
};
