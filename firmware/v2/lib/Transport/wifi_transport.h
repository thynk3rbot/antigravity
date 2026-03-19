/**
 * @file wifi_transport.h
 * @brief WiFi transport layer for LoRaLink v2
 *
 * Manages WiFi connectivity with auto-reconnect, MDNS hostname,
 * and integration with the transport interface for potential future use.
 * Primary purpose: Enable HTTP API server for device configuration and monitoring.
 */

#pragma once

#include "interface.h"
#include <string>
#include <cstdint>
#include <esp_wifi.h>

/**
 * @class WiFiTransport
 * @brief WiFi connectivity layer with auto-reconnect capability
 *
 * Manages:
 * - WiFi station mode initialization and connection
 * - Automatic reconnection with exponential backoff
 * - MDNS hostname registration (loralink-{NODEID}.local)
 * - Event-driven connection/disconnection handling
 *
 * Note: Currently implements basic connection management.
 * Send/receive methods are placeholder implementations as WiFi
 * transport is primarily used for HTTP API (handled separately).
 */
class WiFiTransport : public TransportInterface {
public:
    // ========================================================================
    // Initialization & Control
    // ========================================================================

    /**
     * @brief Initialize WiFi in station mode
     *
     * Configures WiFi, starts connection process, registers event handler,
     * and sets up MDNS hostname.
     *
     * @param ssid WiFi network name
     * @param password WiFi password
     * @param hostname mDNS hostname (e.g., "loralink-Peer1")
     * @return true if initialization successful, false on error
     */
    static bool init(const std::string& ssid, const std::string& password,
                     const std::string& hostname = "loralink");

    /**
     * @brief Check if connected to WiFi
     *
     * @return true if currently connected and IP assigned, false otherwise
     */
    static bool isConnected();

    /**
     * @brief Get current IP address as string
     *
     * @return IP address (e.g., "192.168.1.100") or empty string if not connected
     */
    static std::string getIP();

    /**
     * @brief Manually trigger WiFi reconnection
     *
     * Useful for forcing reconnection after credential change.
     */
    static void reconnect();

    /**
     * @brief Get WiFi connection status as human-readable string
     *
     * @return Status string: "Connected", "Connecting", "Disconnected", "Error"
     */
    static const char* getStatusString();

    /**
     * @brief Get signal strength (RSSI)
     *
     * @return RSSI in dBm (e.g., -45 to -100), or 0 if not connected
     */
    static int8_t getWiFiSignalStrength();

    /**
     * @brief Get number of reconnection attempts made
     *
     * @return Count of reconnection attempts since init
     */
    static uint8_t getReconnectAttempts();

    // ========================================================================
    // TransportInterface Implementation (placeholder)
    // ========================================================================

    /**
     * @brief Initialize transport (part of interface)
     * Delegates to static init() method.
     */
    virtual bool init() override;

    /**
     * @brief Check if transport is ready
     */
    virtual bool isReady() const override;

    /**
     * @brief Send data (placeholder - WiFi uses HTTP API instead)
     */
    virtual int send(const uint8_t* payload, size_t len) override;

    /**
     * @brief Receive data (placeholder - WiFi uses HTTP API instead)
     */
    virtual int recv(uint8_t* buffer, size_t maxLen) override;

    /**
     * @brief Check if data available
     */
    virtual bool isAvailable() const override { return false; }

    /**
     * @brief Poll transport (optional periodic updates)
     */
    virtual void poll() override;

    /**
     * @brief Get transport name
     */
    virtual const char* getName() const override { return "WiFi"; }

    /**
     * @brief Get transport type
     */
    virtual TransportType getType() const override { return TransportType::HTTP; }

    /**
     * @brief Get signal strength (RSSI)
     */
    virtual int8_t getSignalStrength() const override { return getWiFiSignalStrength(); }

    /**
     * @brief Get status string
     */
    virtual const char* getStatus() const override { return getStatusString(); }

    /**
     * @brief Get last error code
     */
    virtual int getLastError() const override { return lastError; }

    /**
     * @brief Clear error state
     */
    virtual void clearError() override { lastError = 0; }

    /**
     * @brief Get last error message
     */
    virtual const char* getLastErrorString() const override;

private:
    // ========================================================================
    // Static State
    // ========================================================================

    static bool connected;
    static std::string currentIP;
    static std::string hostname;
    static uint32_t lastReconnectTime;
    static uint8_t reconnectAttempts;
    static uint8_t reconnectBackoffSeconds;
    static wifi_event_t lastWiFiEvent;
    static int lastError;

    // ========================================================================
    // WiFi Event Handler
    // ========================================================================

    /**
     * @brief WiFi event handler
     *
     * Called by ESP-IDF when WiFi events occur.
     * Handles connection, disconnection, and error events.
     */
    static void onWiFiEvent(void* arg, esp_event_base_t eventBase,
                           int32_t eventId, void* eventData);

    /**
     * @brief Internal method to apply exponential backoff
     */
    static void scheduleReconnect();

    /**
     * @brief Internal method to perform exponential backoff calculation
     */
    static uint8_t getNextBackoffTime();
};
