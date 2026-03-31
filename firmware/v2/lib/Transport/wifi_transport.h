/**
 * @file wifi_transport.h
 * @brief WiFi Transport Layer for Magic v2
 *
 * Manages WiFi connectivity, ArduinoOTA, mDNS, and integrates with
 * TransportInterface for the message routing system.
 *
 * Primary purpose: Provide WiFi connectivity for the HTTP API server.
 * HTTP endpoints are handled by HttpAPI (lib/App/http_api.h).
 *
 * Usage (from main.cpp):
 *   WiFiTransport::init(ssid, pass, "magic-Peer1");
 *   // In loop / FreeRTOS task:
 *   WiFiTransport::poll();   // drives OTA + reconnect
 */

#pragma once

#include "interface.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <functional>
#include <string>
#include <cstdint>

// ============================================================================
// Reconnect / backoff tunables
// ============================================================================

// #define WIFI_CONNECT_TIMEOUT_MS     10000   // 10 s initial connect wait
#ifndef WIFI_CONNECT_TIMEOUT_MS
  #define WIFI_CONNECT_TIMEOUT_MS     10000   // Fallback
#endif
#define WIFI_RECONNECT_INTERVAL_MS  30000   // 30 s between reconnect attempts
#define WIFI_POLL_INTERVAL_MS       500     // poll granularity inside connect()

/**
 * @class WiFiTransport
 * @brief Static WiFi transport layer with OTA + mDNS support
 *
 * All state is static (singleton-style) so main.cpp can call
 * WiFiTransport::init() without instantiating an object.
 * The instance virtual methods delegate to the static implementation,
 * satisfying the TransportInterface contract used by MessageRouter.
 */
class WiFiTransport : public TransportInterface {
public:
    // ========================================================================
    // Static Lifecycle API  (called from main.cpp)
    // ========================================================================

    /**
     * @brief Initialize WiFi transport
     *
     * 1. Sets WiFi STA mode.
     * 2. Calls _connect() – waits up to WIFI_CONNECT_TIMEOUT_MS.
     * 3. On success: starts ArduinoOTA (hostname = mdnsHostname) and mDNS.
     *
     * @param ssid     WiFi network name
     * @param password WiFi password
     * @param mdnsHostname mDNS hostname (e.g. "magic-Peer1")
     * @return true if WiFi connected at init time, false on timeout/error
     *         (transport continues retrying in poll())
     */
    static bool init(const std::string& ssid, const std::string& password,
                     const std::string& mdnsHostname = "magic");

    /**
     * @brief Drive OTA handler and reconnect logic
     *
     * Must be called periodically (e.g. from a FreeRTOS task or loop()).
     * - Calls ArduinoOTA.handle()
     * - Attempts reconnection if WiFi dropped (every WIFI_RECONNECT_INTERVAL_MS)
     */
    static void service();

    /**
     * @brief Check if currently connected
     * @return true if WiFi status == WL_CONNECTED
     */
    static bool isConnected();

    /**
     * @brief Get current IP address
     * @return IP string (e.g. "192.168.1.100") or empty string if not connected
     */
    static std::string getIP();

    /**
     * @brief Get current SSID
     * @return SSID string used at init time
     */
    static std::string getSSID();

    /**
     * @brief Get WiFi RSSI
     * @return RSSI in dBm, or 0 if not connected
     */
    static int8_t getWiFiSignalStrength();

    /**
     * @brief Get reconnect attempt counter
     * @return Number of reconnect attempts since init
     */
    static uint8_t getReconnectAttempts();

    /**
     * @brief Force reconnect using stored credentials
     */
    static void reconnect();

    /**
     * @brief Human-readable connection status
     * @return "Connected", "Connecting", "Disconnected", or "Error"
     */
    static const char* getStatusString();

    // ========================================================================
    // TransportInterface Virtual Methods  (instance delegates to static state)
    // ========================================================================

    /** @brief Delegates to static isConnected() */
    virtual bool init() override;

    /** @brief Delegates to static isConnected() */
    virtual bool isReady() const override;

    /**
     * @brief Not applicable – WiFi commands arrive via HTTP callbacks
     * @return -1 (not implemented)
     */
    virtual int send(const uint8_t* payload, size_t len) override;

    /**
     * @brief Not applicable – WiFi data arrives via HTTP
     * @return 0 (no data)
     */
    virtual int recv(uint8_t* buffer, size_t maxLen) override;

    /** @brief Always false – no P2P receive queue */
    virtual bool isAvailable() const override { return false; }

    /** @brief Calls service() – drives OTA + reconnect */
    virtual void poll() override;

    virtual const char* getName() const override { return "WiFi"; }

    virtual TransportType getType() const override { return TransportType::HTTP; }

    virtual int8_t getSignalStrength() const override {
        return getWiFiSignalStrength();
    }

    virtual const char* getStatus() const override { return getStatusString(); }

    virtual int  getLastError() const override { return _lastError; }
    virtual void clearError()         override { _lastError = 0; }
    virtual const char* getLastErrorString() const override;

private:
    // ========================================================================
    // Static State
    // ========================================================================

    static std::string _ssid;
    static std::string _password;
    static std::string _hostname;

    static bool     _otaStarted;
    static bool     _mdnsStarted;
    static bool     _apActive;
    static DNSServer _dnsServer;
    static uint32_t _lastReconnectAttempt;
    static uint8_t  _reconnectAttempts;
    static int      _lastError;

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /**
     * @brief Block (with 500 ms polls) until connected or timeout
     *
     * @param timeoutMs Maximum wait in milliseconds
     * @return true if connected within timeout
     */
    static bool _connect(uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS);

    /**
     * @brief Start ArduinoOTA with the configured hostname
     */
    static void _startOTA();

    /**
     * @brief Register mDNS service
     */
    static void _startMDNS();

    /**
     * @brief Start Access Point for Captive Portal
     */
    static void _startAP();
};
