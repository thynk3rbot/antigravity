/**
 * @file nvs_manager.cpp
 * @brief NVS (Non-Volatile Storage) persistence layer implementation
 *
 * Uses ESP-IDF's built-in NVS API for persistent device configuration storage.
 * All operations include error checking and logging via Serial.
 *
 * ESP-IDF NVS API Reference:
 * - nvs_flash_init()      : Initialize NVS flash partition
 * - nvs_open()            : Open namespace for read/write
 * - nvs_get_str()         : Get string value
 * - nvs_set_str()         : Set string value
 * - nvs_get_u16()         : Get 16-bit unsigned int
 * - nvs_set_u16()         : Set 16-bit unsigned int
 * - nvs_get_blob()        : Get binary blob (e.g., encryption key)
 * - nvs_set_blob()        : Set binary blob
 * - nvs_commit()          : Commit changes to flash
 * - nvs_close()           : Close namespace handle
 * - nvs_erase_all()       : Erase all keys in namespace
 */

#include "nvs_manager.h"
#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_random.h>

// ============================================================================
// Static Initialization
// ============================================================================

bool NVSManager::init() {
  Serial.println("[NVS] Initializing NVS flash partition...");

  // Initialize NVS flash partition
  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition is full or has new version; erase and reinitialize
    Serial.println("[NVS] NVS partition full/outdated - erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    Serial.printf("[NVS] ERROR: Failed to initialize NVS flash: %d\n", err);
    return false;
  }

  Serial.println("[NVS] NVS flash partition initialized");

  // Test opening namespace
  nvs_handle_t handle;
  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

  if (err != ESP_OK) {
    Serial.printf("[NVS] ERROR: Failed to open NVS namespace '%s': %d\n",
                  NVS_NAMESPACE, err);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] Namespace '%s' opened successfully\n", NVS_NAMESPACE);

  return true;
}

// ============================================================================
// Device Identity
// ============================================================================

bool NVSManager::setNodeID(const std::string& id) {
  if (id.length() > 16) {
    Serial.println("[NVS] ERROR: Node ID too long (max 16 bytes)");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setNodeID", "open", err);
    return false;
  }

  err = nvs_set_str(handle, "node_id", id.c_str());
  if (err != ESP_OK) {
    logError("setNodeID", "node_id", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setNodeID", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] Node ID set to: %s\n", id.c_str());
  return true;
}

std::string NVSManager::getNodeID(const std::string& defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getNodeID", "open", err);
    return defaultVal;
  }

  // Get required size for string
  size_t required_size = 0;
  err = nvs_get_str(handle, "node_id", nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getNodeID", "node_id", err);
    nvs_close(handle);
    return defaultVal;
  }

  // Allocate buffer and get string
  char* buf = new char[required_size];
  err = nvs_get_str(handle, "node_id", buf, &required_size);

  nvs_close(handle);

  if (err != ESP_OK) {
    logError("getNodeID", "node_id_read", err);
    delete[] buf;
    return defaultVal;
  }

  std::string result(buf);
  delete[] buf;
  return result;
}

// ============================================================================
// WiFi Configuration
// ============================================================================

bool NVSManager::setWiFiSSID(const std::string& ssid) {
  if (ssid.length() > 32) {
    Serial.println("[NVS] ERROR: WiFi SSID too long (max 32 bytes)");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setWiFiSSID", "open", err);
    return false;
  }

  err = nvs_set_str(handle, "wifi_ssid", ssid.c_str());
  if (err != ESP_OK) {
    logError("setWiFiSSID", "wifi_ssid", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setWiFiSSID", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] WiFi SSID set to: %s\n", ssid.c_str());
  return true;
}

std::string NVSManager::getWiFiSSID(const std::string& defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getWiFiSSID", "open", err);
    return defaultVal;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, "wifi_ssid", nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getWiFiSSID", "wifi_ssid", err);
    nvs_close(handle);
    return defaultVal;
  }

  char* buf = new char[required_size];
  err = nvs_get_str(handle, "wifi_ssid", buf, &required_size);

  nvs_close(handle);

  if (err != ESP_OK) {
    logError("getWiFiSSID", "wifi_ssid_read", err);
    delete[] buf;
    return defaultVal;
  }

  std::string result(buf);
  delete[] buf;
  return result;
}

bool NVSManager::setWiFiPassword(const std::string& pass) {
  if (pass.length() > 64) {
    Serial.println("[NVS] ERROR: WiFi password too long (max 64 bytes)");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setWiFiPassword", "open", err);
    return false;
  }

  err = nvs_set_str(handle, "wifi_pass", pass.c_str());
  if (err != ESP_OK) {
    logError("setWiFiPassword", "wifi_pass", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setWiFiPassword", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.println("[NVS] WiFi password set (hidden for security)");
  return true;
}

std::string NVSManager::getWiFiPassword(const std::string& defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getWiFiPassword", "open", err);
    return defaultVal;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, "wifi_pass", nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getWiFiPassword", "wifi_pass", err);
    nvs_close(handle);
    return defaultVal;
  }

  char* buf = new char[required_size];
  err = nvs_get_str(handle, "wifi_pass", buf, &required_size);

  nvs_close(handle);

  if (err != ESP_OK) {
    logError("getWiFiPassword", "wifi_pass_read", err);
    delete[] buf;
    return defaultVal;
  }

  std::string result(buf);
  delete[] buf;
  return result;
}

// ============================================================================
// Encryption Configuration
// ============================================================================

bool NVSManager::setCryptoKey(const uint8_t key[16]) {
  if (key == nullptr) {
    Serial.println("[NVS] ERROR: Crypto key pointer is null");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setCryptoKey", "open", err);
    return false;
  }

  err = nvs_set_blob(handle, "crypto_key", (void*)key, 16);
  if (err != ESP_OK) {
    logError("setCryptoKey", "crypto_key", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setCryptoKey", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.println("[NVS] Crypto key set (16 bytes)");
  return true;
}

bool NVSManager::getCryptoKey(uint8_t outKey[16]) {
  if (outKey == nullptr) {
    Serial.println("[NVS] ERROR: Output key buffer is null");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getCryptoKey", "open", err);
    return false;
  }

  size_t required_size = 16;
  err = nvs_get_blob(handle, "crypto_key", outKey, &required_size);

  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    Serial.println("[NVS] Crypto key not set");
    return false;
  }

  if (err != ESP_OK) {
    logError("getCryptoKey", "crypto_key", err);
    return false;
  }

  if (required_size != 16) {
    Serial.printf("[NVS] WARNING: Crypto key size mismatch: %u bytes\n",
                  required_size);
    return false;
  }

  Serial.println("[NVS] Crypto key retrieved (16 bytes)");
  return true;
}

bool NVSManager::generateAndStoreCryptoKey() {
  Serial.println("[NVS] Generating random AES-128 key...");

  uint8_t key[16];
  // Use ESP32 hardware RNG to generate random key
  for (int i = 0; i < 16; i++) {
    key[i] = (uint8_t)esp_random();
  }

  return setCryptoKey(key);
}

// ============================================================================
// MQTT Configuration
// ============================================================================

bool NVSManager::setMQTTBroker(const std::string& broker) {
  if (broker.length() > 64) {
    Serial.println("[NVS] ERROR: MQTT broker address too long (max 64 bytes)");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setMQTTBroker", "open", err);
    return false;
  }

  err = nvs_set_str(handle, "mqtt_broker", broker.c_str());
  if (err != ESP_OK) {
    logError("setMQTTBroker", "mqtt_broker", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setMQTTBroker", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] MQTT broker set to: %s\n", broker.c_str());
  return true;
}

std::string NVSManager::getMQTTBroker(const std::string& defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getMQTTBroker", "open", err);
    return defaultVal;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, "mqtt_broker", nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getMQTTBroker", "mqtt_broker", err);
    nvs_close(handle);
    return defaultVal;
  }

  char* buf = new char[required_size];
  err = nvs_get_str(handle, "mqtt_broker", buf, &required_size);

  nvs_close(handle);

  if (err != ESP_OK) {
    logError("getMQTTBroker", "mqtt_broker_read", err);
    delete[] buf;
    return defaultVal;
  }

  std::string result(buf);
  delete[] buf;
  return result;
}

bool NVSManager::setMQTTPort(uint16_t port) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setMQTTPort", "open", err);
    return false;
  }

  err = nvs_set_u16(handle, "mqtt_port", port);
  if (err != ESP_OK) {
    logError("setMQTTPort", "mqtt_port", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setMQTTPort", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] MQTT port set to: %u\n", port);
  return true;
}

uint16_t NVSManager::getMQTTPort(uint16_t defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getMQTTPort", "open", err);
    return defaultVal;
  }

  uint16_t port = defaultVal;
  err = nvs_get_u16(handle, "mqtt_port", &port);

  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getMQTTPort", "mqtt_port", err);
    return defaultVal;
  }

  return port;
}

// ============================================================================
// Hardware Version
// ============================================================================

bool NVSManager::setHardwareVersion(const std::string& hwver) {
  if (hwver.length() > 8) {
    Serial.println("[NVS] ERROR: Hardware version too long (max 8 bytes)");
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("setHardwareVersion", "open", err);
    return false;
  }

  err = nvs_set_str(handle, "hw_version", hwver.c_str());
  if (err != ESP_OK) {
    logError("setHardwareVersion", "hw_version", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("setHardwareVersion", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.printf("[NVS] Hardware version set to: %s\n", hwver.c_str());
  return true;
}

std::string NVSManager::getHardwareVersion(const std::string& defaultVal) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    logError("getHardwareVersion", "open", err);
    return defaultVal;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, "hw_version", nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return defaultVal;
  }

  if (err != ESP_OK) {
    logError("getHardwareVersion", "hw_version", err);
    nvs_close(handle);
    return defaultVal;
  }

  char* buf = new char[required_size];
  err = nvs_get_str(handle, "hw_version", buf, &required_size);

  nvs_close(handle);

  if (err != ESP_OK) {
    logError("getHardwareVersion", "hw_version_read", err);
    delete[] buf;
    return defaultVal;
  }

  std::string result(buf);
  delete[] buf;
  return result;
}

// ============================================================================
// Utility Operations
// ============================================================================

bool NVSManager::clearAll() {
  Serial.println("[NVS] WARNING: Clearing all NVS data in 'loralink' namespace...");

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    logError("clearAll", "open", err);
    return false;
  }

  err = nvs_erase_all(handle);
  if (err != ESP_OK) {
    logError("clearAll", "erase_all", err);
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    logError("clearAll", "commit", err);
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  Serial.println("[NVS] All NVS data cleared");
  return true;
}

void NVSManager::printInfo() {
  Serial.println("\n========== NVS Configuration ==========");

  Serial.printf("  Node ID: %s\n", getNodeID("(not set)").c_str());
  Serial.printf("  WiFi SSID: %s\n", getWiFiSSID("(not set)").c_str());
  Serial.printf("  WiFi Password: %s\n",
                getWiFiPassword("(not set)").empty() ? "(not set)" : "(set)");

  uint8_t key[16];
  bool hasCrypto = getCryptoKey(key);
  Serial.printf("  Crypto Key: %s\n", hasCrypto ? "(set, 16 bytes)" : "(not set)");

  Serial.printf("  MQTT Broker: %s\n", getMQTTBroker("(not set)").c_str());
  Serial.printf("  MQTT Port: %u\n", getMQTTPort());
  Serial.printf("  Hardware Version: %s\n", getHardwareVersion().c_str());

  Serial.println("=======================================\n");
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void NVSManager::logError(const char* operation, const char* key, int err) {
  Serial.printf("[NVS] ERROR in %s (%s): esp_err_t=%d\n", operation, key, err);
}
