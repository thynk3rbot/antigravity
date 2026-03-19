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
#include <string>

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
    static StaticJsonDocument<2048> buildStatus();

    /**
     * @brief Build status and serialize to string
     *
     * Convenience method that calls buildStatus() and serializes.
     *
     * @return JSON string representation of full status
     */
    static std::string buildStatusString();

private:
    // Helper methods for each status section
    // These are called by buildStatus() to populate the document

    /**
     * @brief Add basic device info (id, hw, ver, ip, mac)
     */
    static void addBasicInfo(JsonDocument& doc);

    /**
     * @brief Add power/battery info (bat, bat_percentage, mode)
     */
    static void addPowerInfo(JsonDocument& doc);

    /**
     * @brief Add LoRa signal info (lora_rssi, lora_snr, rssi_history)
     */
    static void addLoRaInfo(JsonDocument& doc);

    /**
     * @brief Add WiFi connectivity info (wifi_rssi, wifi_connected)
     */
    static void addWiFiInfo(JsonDocument& doc);

    /**
     * @brief Add BLE info (ble_enabled, ble_device_name)
     */
    static void addBLEInfo(JsonDocument& doc);

    /**
     * @brief Add MQTT info (mqtt_connected, mqtt_broker)
     */
    static void addMQTTInfo(JsonDocument& doc);

    /**
     * @brief Add peer list from mesh coordinator
     */
    static void addPeerInfo(JsonDocument& doc);

    /**
     * @brief Add transport status object (wifi, ble, mqtt, lora booleans)
     */
    static void addTransportStatus(JsonDocument& doc);

    /**
     * @brief Add relay status (status, mode, last_toggled, on_duration_ms)
     */
    static void addRelayInfo(JsonDocument& doc);

    /**
     * @brief Add telemetry (temp_sensor, temp_c, humidity_percent, pressure_hpa)
     */
    static void addTelemetry(JsonDocument& doc);

    /**
     * @brief Add system info (uptime, boot_count, heap, last_update, etc.)
     */
    static void addSystemInfo(JsonDocument& doc);

    // Static state for historical data
    static int8_t rssiHistory[5];           ///< Last 5 LoRa RSSI readings
    static uint8_t rssiHistoryIndex;        ///< Current position in history
    static uint32_t lastBootTimestamp;      ///< Boot time (millis)
};
