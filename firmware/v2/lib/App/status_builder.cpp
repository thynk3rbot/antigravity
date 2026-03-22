/**
 * @file status_builder.cpp
 * @brief Status JSON builder implementation
 *
 * Aggregates device state from all managers (Power, WiFi, LoRa, Mesh, etc.)
 * into a single v0.1.0-compatible JSON response for /api/status endpoint.
 */

#include "status_builder.h"
#include "power_manager.h"
#include "nvs_manager.h"
#include "http_api.h"
#include "mesh_coordinator.h"
#include "gps_manager.h"
#include "../Transport/wifi_transport.h"
#include "../Transport/lora_transport.h"
#include "../Transport/ble_transport.h"
#ifdef ENABLE_MQTT_TRANSPORT
#include "../Transport/mqtt_transport.h"
#endif
#include "../HAL/board_config.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <esp_wifi.h>

// ============================================================================
// Static State Initialization
// ============================================================================

int8_t StatusBuilder::rssiHistory[5] = {0, 0, 0, 0, 0};
uint8_t StatusBuilder::rssiHistoryIndex = 0;
uint32_t StatusBuilder::lastBootTimestamp = 0;

// ============================================================================
// Public Methods
// ============================================================================

StaticJsonDocument<2048> StatusBuilder::buildStatus() {
    StaticJsonDocument<2048> doc;

    // Add all sections to the document
    addBasicInfo(doc);
    addPowerInfo(doc);
    addLoRaInfo(doc);
    addWiFiInfo(doc);
    addBLEInfo(doc);
    addMQTTInfo(doc);
    addPeerInfo(doc);
    addGPSInfo(doc);
    addTransportStatus(doc);
    addRelayInfo(doc);
    addTelemetry(doc);
    addSystemInfo(doc);
    addPluginList(doc);
    addHardwareMap(doc);

    return doc;
}

std::string StatusBuilder::buildStatusString() {
    StaticJsonDocument<2048> doc = buildStatus();
    std::string result;
    serializeJson(doc, result);
    return result;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void StatusBuilder::addBasicInfo(JsonDocument& doc) {
    // Device identity
    doc["id"] = NVSManager::getNodeID("Node").c_str();
    doc["hw_id"] = NVSManager::getHardwareID().c_str();

    // Hardware version (detect from build flags)
    #ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
        doc["hw"] = "V4";
    #elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
        doc["hw"] = "V3";
    #elif defined(ARDUINO_HELTEC_WIFI_LORA_32)
        doc["hw"] = "V2";
    #else
        doc["hw"] = NVSManager::getHardwareVersion("V3").c_str();
    #endif

    // Firmware version (from build define)
    #ifdef FIRMWARE_VERSION
        doc["version"] = FIRMWARE_VERSION;
    #else
        doc["version"] = "0.2.2";
    #endif

    // IP address (empty if not connected)
    std::string ip = WiFiTransport::getIP();
    doc["ip"] = ip.empty() ? "" : ip.c_str();

    // MAC address formatted as "XX:XX:XX:XX:XX:XX"
    uint8_t macAddr[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, macAddr) == ESP_OK) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 macAddr[0], macAddr[1], macAddr[2],
                 macAddr[3], macAddr[4], macAddr[5]);
        doc["mac"] = macStr;
    } else {
        doc["mac"] = "XX:XX:XX:XX:XX:XX";
    }
}

void StatusBuilder::addPowerInfo(JsonDocument& doc) {
    float batVoltage = PowerManager::getBatteryVoltage();

    // Battery voltage formatted as "X.XXV"
    char batStr[8];
    snprintf(batStr, sizeof(batStr), "%.2fV", batVoltage);
    doc["bat"] = batStr;

    // Battery percentage (linear interpolation: 3.2V = 100%, 2.8V = 0%)
    // LiPo cell voltage range: 2.8V (empty) to 3.2V (full)
    float batPercentage = ((batVoltage - 2.8f) / (3.2f - 2.8f)) * 100.0f;
    if (batPercentage < 0.0f) batPercentage = 0.0f;
    if (batPercentage > 100.0f) batPercentage = 100.0f;
    doc["bat_percentage"] = static_cast<uint8_t>(batPercentage);

    // Power mode as string
    PowerMode mode = PowerManager::getMode();
    const char* modeStr = "NORMAL";
    if (mode == PowerMode::CONSERVE) {
        modeStr = "CONSERVE";
    } else if (mode == PowerMode::CRITICAL) {
        modeStr = "CRITICAL";
    }
    doc["mode"] = modeStr;
}

void StatusBuilder::addLoRaInfo(JsonDocument& doc) {
    // Get current LoRa RSSI and SNR (from last received packet)
    int8_t loraRSSI = loraTransport.getSignalStrength();

    // SNR is not readily available in current loraTransport interface
    // Default to 0 (TODO: enhance loraTransport to track SNR)
    int8_t loraSNR = 0;

    doc["lora_rssi"] = loraRSSI;
    doc["lora_snr"] = loraSNR;

    // Update RSSI history (rolling window of last 5 readings)
    rssiHistory[rssiHistoryIndex] = loraRSSI;
    rssiHistoryIndex = (rssiHistoryIndex + 1) % 5;

    // Add RSSI history array
    JsonArray rssiArr = doc.createNestedArray("rssi_history");
    for (int i = 0; i < 5; i++) {
        rssiArr.add(rssiHistory[i]);
    }
}

void StatusBuilder::addWiFiInfo(JsonDocument& doc) {
    bool wifiConnected = WiFiTransport::isConnected();
    int8_t wifiRSSI = WiFiTransport::getWiFiSignalStrength();

    doc["wifi_rssi"] = wifiRSSI;
    doc["wifi_connected"] = wifiConnected;
}

void StatusBuilder::addBLEInfo(JsonDocument& doc) {
    // Device name format: "GW-{NODEID}"
    std::string nodeID = NVSManager::getNodeID("Node");
    std::string bleDeviceName = "GW-" + nodeID;
    doc["ble_device_name"] = bleDeviceName.c_str();
    doc["ble_enabled"] = true; // BLE is always enabled in v2
    doc["ble_connected"] = BLETransport::isConnected();
}

void StatusBuilder::addMQTTInfo(JsonDocument& doc) {
    #ifdef ENABLE_MQTT_TRANSPORT
      doc["mqtt_connected"] = MQTTTransport::instance()->isConnected();
    #else
      doc["mqtt_connected"] = false;
    #endif

    // MQTT broker address (from NVS)
    std::string broker = NVSManager::getMQTTBroker("");
    uint16_t port = NVSManager::getMQTTPort(1883);

    if (!broker.empty()) {
        char brokerStr[80];
        snprintf(brokerStr, sizeof(brokerStr), "%s:%u", broker.c_str(), port);
        doc["mqtt_broker"] = brokerStr;
    } else {
        doc["mqtt_broker"] = "";
    }
}

void StatusBuilder::addGPSInfo(JsonDocument& doc) {
    GPSManager::GPSData gps = GPSManager::getData();
    JsonObject obj = doc.createNestedObject("gps");
    obj["lat"] = gps.lat;
    obj["lon"] = gps.lon;
    obj["alt"] = gps.alt;
    obj["sats"] = gps.satellites;
    obj["fix"] = gps.hasFix;
    obj["age"] = gps.fixAge;
}

void StatusBuilder::addPeerInfo(JsonDocument& doc) {
    // Get peer list from MeshCoordinator
    JsonArray peersArray = doc.createNestedArray("peers");

    // Get neighbors from MeshCoordinator
    const auto& neighbors = MeshCoordinator::instance().getNeighbors();
    
    for (const auto& pair : neighbors) {
        const NeighborInfo& neighbor = pair.second;
        JsonObject peerObj = peersArray.createNestedObject();
        
        char idStr[16];
        snprintf(idStr, sizeof(idStr), "Node%u", neighbor.nodeID);
        peerObj["id"] = idStr;
        peerObj["node_id"] = neighbor.nodeID;
        peerObj["hop"] = neighbor.hopCount;
        peerObj["rssi"] = neighbor.rssi;
        peerObj["last_seen"] = (millis() - neighbor.lastSeenMs) / 1000; // seconds ago
        peerObj["packets"] = neighbor.packetCount;
    }
}

void StatusBuilder::addTransportStatus(JsonDocument& doc) {
    JsonObject transports = doc.createNestedObject("transports");

    transports["wifi"] = WiFiTransport::isConnected();
    transports["ble"] = BLETransport::isConnected();
    #ifdef ENABLE_MQTT_TRANSPORT
      transports["mqtt"] = MQTTTransport::instance()->isConnected();
    #else
      transports["mqtt"] = false;
    #endif
    transports["lora"] = true;  // LoRa is always available
}

void StatusBuilder::addRelayInfo(JsonDocument& doc) {
    JsonObject relay = doc.createNestedObject("relay");

    // TODO: Integrate with RelayManager when available
    // For now, provide placeholder values
    relay["status"] = "OFF";
    relay["mode"] = "MANUAL";
    relay["last_toggled"] = 0;       // Unix timestamp of last toggle
    relay["on_duration_ms"] = 0;     // How long relay has been ON
}

void StatusBuilder::addTelemetry(JsonDocument& doc) {
    JsonObject telemetry = doc.createNestedObject("telemetry");

    // Temperature sensor status
    telemetry["temp_sensor"] = "enabled";  // TODO: Make configurable

    // Temperature in Celsius
    telemetry["temp_c"] = 24.5;  // TODO: Read from actual sensor

    // Humidity percentage
    telemetry["humidity_percent"] = 65;  // TODO: Read from actual sensor

    // Atmospheric pressure in hPa
    telemetry["pressure_hpa"] = 1013.25;  // TODO: Read from actual sensor
}

void StatusBuilder::addSystemInfo(JsonDocument& doc) {
    uint32_t now = millis();

    // Uptime in seconds
    uint32_t uptimeSeconds = now / 1000;
    doc["uptime_seconds"] = uptimeSeconds;

    // Boot count (persisted in NVS)
    // TODO: Implement boot counter in NVSManager
    doc["boot_count"] = 42;  // Placeholder

    // Free heap size in bytes
    uint32_t freeHeap = esp_get_free_heap_size();
    doc["heap"] = freeHeap;

    // Heap percentage (free/total * 100)
    // ESP32 total heap is typically 320KB
    uint32_t totalHeap = 320000;  // Approximate for ESP32-S3
    uint8_t heapPercent = static_cast<uint8_t>((freeHeap * 100) / totalHeap);
    if (heapPercent > 100) heapPercent = 100;
    doc["heap_percentage"] = heapPercent;

    // Timestamp of when this status was generated
    doc["last_status_update"] = static_cast<uint32_t>(now / 1000);  // Unix-like seconds

    // Friendly device name (optional, from NVS)
    // TODO: Add to NVSManager
    std::string nodeID = NVSManager::getNodeID("Node");
    std::string friendlyName = nodeID + " (Kitchen)";  // Example
    doc["friendly_name"] = friendlyName.c_str();

    // Location tag (optional, from NVS)
    doc["location"] = "Kitchen";  // TODO: Add to NVSManager

    // Last executed command
    doc["last_command"] = "STATUS";  // TODO: Track in CommandManager

    // Pending commands in queue
    doc["command_queue_length"] = 0;  // TODO: Get from CommandManager

    // Encryption status (always true in v2)
    doc["crypto_enabled"] = true;

    // Schedule info
    JsonObject schedule = doc.createNestedObject("schedule");
    schedule["enabled"] = false;  // TODO: Get from ScheduleManager
    schedule.createNestedArray("entries");
    // TODO: Populate with schedule entries from ScheduleManager
}

void StatusBuilder::addPluginList(JsonDocument& doc) {
    JsonArray plugins = doc.createNestedArray("plugins");
    // TODO: Get list from pluginManager
}

void StatusBuilder::addHardwareMap(JsonDocument& doc) {
    JsonObject map = doc.createNestedObject("hardware_map");
    // TODO: Populate with actual GPIO mapping from board_config.h
}
