/**
 * @file status_builder.h
 * @brief Status JSON builder for /api/status endpoint
 *
 * Builds complete device status JSON response compatible with v0.1.0 schema.
 * Handles all data sources: power, LoRa, WiFi, BLE, MQTT, mesh, relay, telemetry.
 * Gracefully handles unavailable transports/sensors (returns false/null values).
 *
 * Schema includes:
 * - Device identity: id, hw, ver, ip, mac
 * - Power: battery voltage, percentage, mode
 * - Connectivity: LoRa, WiFi, BLE, MQTT signal strength
 * - Mesh: peer list with hop count and RSSI
 * - Transports: status of each available transport
 * - Relay: status, mode, last toggle timestamp
 * - Telemetry: temperature, humidity, pressure (if available)
 * - System: uptime, boot count, heap, timestamps
 */

#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @class StatusBuilder
 * @brief Static builder for device status JSON documents
 *
 * All methods are static (no instances needed).
 * Uses StaticJsonDocument<2048> for fixed memory allocation.
 */
class StatusBuilder {
public:
    /**
     * @brief Build complete device status JSON document
     *
     * Aggregates all device state into a single JSON response.
     * Uses StaticJsonDocument<2048> internally.
     *
     * @return JsonDocument containing full status (or empty doc on error)
     */
    static ArduinoJson::StaticJsonDocument<2048> buildStatus(bool verbose = true);

    /**
     * @brief Build status and serialize to string
     *
     * Convenience method that calls buildStatus() and serializes.
     *
     * @return JSON string representation of full status
     */
    static std::string buildStatusString(bool verbose = true);

    // Helper methods for each status section
    // These are called by buildStatus() to populate the document

    /**
     * @brief Add basic device info (id, hw, ver, ip, mac)
     */
    static void addBasicInfo(ArduinoJson::JsonDocument& doc);

    static void addPowerInfo(ArduinoJson::JsonDocument& doc);
    static void addLoRaInfo(ArduinoJson::JsonDocument& doc);
    static void addWiFiInfo(ArduinoJson::JsonDocument& doc);
    static void addBLEInfo(ArduinoJson::JsonDocument& doc);
    static void addMQTTInfo(ArduinoJson::JsonDocument& doc);
    static void addPeerInfo(ArduinoJson::JsonDocument& doc);
    static void addGPSInfo(ArduinoJson::JsonDocument& doc);
    static void addTransportStatus(ArduinoJson::JsonDocument& doc);
    static void addRelayInfo(ArduinoJson::JsonDocument& doc);
    static void addTelemetry(ArduinoJson::JsonDocument& doc);
    static void addSystemInfo(ArduinoJson::JsonDocument& doc);
    static void addPluginList(ArduinoJson::JsonDocument& doc);
    static void addHardwareMap(ArduinoJson::JsonDocument& doc);

private:
    // Static state for historical data
    static int8_t rssiHistory[5];           ///< Last 5 LoRa RSSI readings
    static uint8_t rssiHistoryIndex;        ///< Current position in history
    static uint32_t lastBootTimestamp;      ///< Boot time (millis)
};
