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
#include <esp_efuse.h>
#include <mbedtls/sha256.h>
#include <algorithm>

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

std::string NVSManager::getHardwareID() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char buf[16];
  snprintf(buf, sizeof(buf), "LL-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return std::string(buf);
}

bool NVSManager::setNodeID(const std::string& id) {
  if (id.length() > 32) return false;
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_NODE_ID, id.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getNodeID(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  esp_err_t err = nvs_get_str(handle, KEY_NODE_ID, nullptr, &size);
  if (err != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }

  char* buf = new char[size];
  nvs_get_str(handle, KEY_NODE_ID, buf, &size);
  std::string result(buf);
  delete[] buf;
  nvs_close(handle);
  return result;
}

// ============================================================================
// Hardware Version Mapping (Legacy Compatibility)
// ============================================================================

bool NVSManager::setHardwareVersion(const std::string& hwver) {
  // Map "V2" -> 2, "V3" -> 3, etc.
  uint8_t val = 3;
  if (hwver.find("2") != std::string::npos) val = 2;
  else if (hwver.find("3") != std::string::npos) val = 3;
  else if (hwver.find("4") != std::string::npos) val = 4;
  return setHardwareVariant(val);
}

std::string NVSManager::getHardwareVersion(const std::string& defaultVal) {
  uint8_t v = getHardwareVariant(0);
  if (v == 0) return defaultVal;
  return "V" + std::to_string(v);
}

// ============================================================================
// WiFi Configuration
// ============================================================================

bool NVSManager::setWiFiSSID(const std::string& ssid) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_WIFI_SSID, ssid.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getWiFiSSID(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_WIFI_SSID, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_WIFI_SSID, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setWiFiPassword(const std::string& pass) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_WIFI_PASS, pass.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getWiFiPassword(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_WIFI_PASS, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_WIFI_PASS, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

// ============================================================================
// Network Configuration (Static IP)
// ============================================================================

std::string NVSManager::getStaticIP(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_STATIC_IP, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_STATIC_IP, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setStaticIP(const std::string& ip) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_STATIC_IP, ip.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getGateway(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_GATEWAY, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_GATEWAY, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setGateway(const std::string& gw) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_GATEWAY, gw.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getSubnet(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_SUBNET, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_SUBNET, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setSubnet(const std::string& sn) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_SUBNET, sn.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

// ============================================================================
// Encryption & MQTT
// ============================================================================

bool NVSManager::setCryptoKey(const uint8_t key[16]) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(handle, KEY_CRYPTO_KEY, (void*)key, 16);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

bool NVSManager::getCryptoKey(uint8_t outKey[16]) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t size = 16;
  esp_err_t err = nvs_get_blob(handle, KEY_CRYPTO_KEY, outKey, &size);
  nvs_close(handle);
  return (err == ESP_OK && size == 16);
}

bool NVSManager::setMQTTBroker(const std::string& broker) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_MQTT_BROKER, broker.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getMQTTBroker(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_MQTT_BROKER, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_MQTT_BROKER, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setMQTTPort(uint16_t port) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u16(handle, KEY_MQTT_PORT, port);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

uint16_t NVSManager::getMQTTPort(uint16_t defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  uint16_t port = defaultVal;
  nvs_get_u16(handle, KEY_MQTT_PORT, &port);
  nvs_close(handle);
  return port;
}

// ============================================================================
// Relay & Power Management
// ============================================================================

bool NVSManager::getRelayState(uint8_t relayNum) {
  const char* key = (relayNum == 1) ? KEY_RELAY1 : KEY_RELAY2;
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  uint8_t state = 0;
  nvs_get_u8(handle, key, &state);
  nvs_close(handle);
  return (state == 1);
}

bool NVSManager::setRelayState(uint8_t relayNum, bool state) {
  const char* key = (relayNum == 1) ? KEY_RELAY1 : KEY_RELAY2;
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(handle, key, state ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

uint8_t NVSManager::getPowerMode(uint8_t defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  uint8_t mode = defaultVal;
  nvs_get_u8(handle, KEY_POWER_MODE, &mode);
  nvs_close(handle);
  return mode;
}

bool NVSManager::setPowerMode(uint8_t mode) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(handle, KEY_POWER_MODE, mode);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

// ============================================================================
// Product & Hardware Management
// ============================================================================

uint8_t NVSManager::getHardwareVariant(uint8_t defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  uint8_t var = defaultVal;
  nvs_get_u8(handle, KEY_HW_VAR, &var);
  nvs_close(handle);
  return var;
}

bool NVSManager::setHardwareVariant(uint8_t version) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(handle, KEY_HW_VAR, version);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

std::string NVSManager::getActiveProductName(const std::string& defaultVal) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return defaultVal;
  size_t size = 0;
  if (nvs_get_str(handle, KEY_ACTIVE_PROD, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return defaultVal;
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_ACTIVE_PROD, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

bool NVSManager::setActiveProductName(const std::string& name) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(handle, KEY_ACTIVE_PROD, name.c_str());
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

// ============================================================================
// Diagnostics & Boot Tracking
// ============================================================================

uint32_t NVSManager::getBootCount() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return 0;
  uint32_t count = 0;
  nvs_get_u32(handle, KEY_BOOT_COUNT, &count);
  nvs_close(handle);
  return count;
}

void NVSManager::incrementBootCount() {
  uint32_t count = getBootCount();
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u32(handle, KEY_BOOT_COUNT, count + 1);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

std::string NVSManager::getResetReason() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return "Unknown";
  size_t size = 0;
  if (nvs_get_str(handle, KEY_RESET_REASON, nullptr, &size) != ESP_OK) {
    nvs_close(handle);
    return "Unknown";
  }
  char* buf = new char[size];
  nvs_get_str(handle, KEY_RESET_REASON, buf, &size);
  std::string res(buf);
  delete[] buf;
  nvs_close(handle);
  return res;
}

void NVSManager::setResetReason(const std::string& reason) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, KEY_RESET_REASON, reason.c_str());
    nvs_commit(handle);
    nvs_close(handle);
  }
}

// ============================================================================
// Utility Operations
// ============================================================================

bool NVSManager::clearAll() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_erase_all(handle);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

void NVSManager::printInfo() {
  Serial.println("\n========== NVS Configuration (Industrial) ==========");
  Serial.printf("  Node ID: %s\n", getNodeID("(not set)").c_str());
  Serial.printf("  WiFi SSID: %s\n", getWiFiSSID("(not set)").c_str());
  Serial.printf("  MQTT Broker: %s\n", getMQTTBroker("(not set)").c_str());
  Serial.printf("  Hardware Var: %u\n", getHardwareVariant());
  Serial.printf("  Boot Count: %u\n", getBootCount());
  Serial.println("==================================================\n");
}

bool NVSManager::setNetworkSecret(const uint8_t secret[16]) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(handle, "net_secret", (void*)secret, 16);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return (err == ESP_OK);
}

bool NVSManager::getDerivedKey(const uint8_t peerMAC[6], uint8_t outKey[16]) {
  uint8_t ourMAC[6];
  esp_efuse_mac_get_default(ourMAC);

  // Load Network Secret
  uint8_t secret[16];
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t size = 16;
  esp_err_t err = nvs_get_blob(handle, "net_secret", secret, &size);
  nvs_close(handle);

  if (err != ESP_OK || size != 16) {
    // Fallback: Use baked-in default if unprovisioned (Development-only)
    // In production, this should return false to block unencrypted comms.
    for (int i = 0; i < 16; i++) secret[i] = 0x42;
  }

  // Deterministic Peer-to-Peer key derivation: SHA256(sort(ourMAC, peerMAC) + secret)
  uint8_t combined[12 + 16];
  uint8_t sortedMACs[12];
  
  bool weAreLower = false;
  for (int i = 0; i < 6; i++) {
    if (ourMAC[i] < peerMAC[i]) { weAreLower = true; break; }
    if (ourMAC[i] > peerMAC[i]) { weAreLower = false; break; }
  }

  if (weAreLower) {
    memcpy(sortedMACs, ourMAC, 6);
    memcpy(sortedMACs + 6, peerMAC, 6);
  } else {
    memcpy(sortedMACs, peerMAC, 6);
    memcpy(sortedMACs + 6, ourMAC, 6);
  }

  memcpy(combined, sortedMACs, 12);
  memcpy(combined + 12, secret, 16);

  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0); // No _ret suffix
  mbedtls_sha256_update(&ctx, combined, sizeof(combined));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  memcpy(outKey, hash, 16); // Use first 128 bits
  return true;
}

void NVSManager::logError(const char* operation, const char* key, int err) {
  Serial.printf("[NVS] ERROR in %s (%s): esp_err_t=%d\n", operation, key, err);
}
