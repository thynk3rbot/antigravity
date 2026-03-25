/**
 * @file http_api.h
 * @brief HTTP API server interface for LoRaLink v2 device control
 *
 * Provides REST API endpoints for:
 * - Device status reporting (/api/status)
 * - Relay control (/api/relay)
 * - Device commands (/api/command)
 * - OTA firmware updates (/api/ota/update)
 *
 * Built on ESP Async WebServer for non-blocking request handling.
 */

#pragma once

#include <string>
#include <cstdint>

// Forward declarations
class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebSocket;
class AsyncWebSocketClient;

/**
 * @class HttpAPI
 * @brief HTTP API server manager
 *
 * Manages:
 * - AsyncWebServer initialization and routing
 * - Device status JSON caching
 * - Request handlers for all endpoints
 * - CORS headers and error responses
 *
 * All methods are static (singleton pattern).
 */
class HttpAPI {
public:
    // ========================================================================
    // Server Lifecycle
    // ========================================================================

    /**
     * @brief Initialize HTTP API server
     *
     * Starts AsyncWebServer on port 80.
     * Registers all route handlers.
     * Should be called after WiFi connection is established.
     *
     * @return true if server started successfully, false on error
     */
    static bool init();

    /**
     * @brief Shutdown HTTP API server
     *
     * Stops AsyncWebServer and releases resources.
     */
    static void shutdown();

    /**
     * @brief Check if HTTP server is running
     *
     * @return true if server is active and listening, false otherwise
     */
    static bool isRunning();

    // ========================================================================
    // Status Management
    // ========================================================================

    /**
     * @brief Update cached device status JSON
     *
     * Should be called whenever device state changes
     * (battery voltage, relay state, WiFi signal, etc.)
     * to keep API responses current without expensive re-computation.
     *
     * @param jsonStatus Complete device status as JSON string
     *
     * Example format:
     * {
     *   "id": "Peer1",
     *   "hw": "V3",
     *   "ver": "0.3.0",
     *   "ip": "192.168.1.100",
     *   "bat": "3.25V",
     *   "mode": "NORMAL",
     *   "lora_rssi": -95,
     *   "lora_snr": 8,
     *   "peers": [{"id": "Peer2", "hop": 1, "rssi": -75}],
     *   "transports": {"wifi": true, "lora": true, "mqtt": false},
     *   "relay": {"status": "OFF", "mode": "MANUAL"},
     *   "uptime": 3600
     * }
     */
    static void updateStatus(const std::string& jsonStatus);

    /**
     * @brief Get current cached status JSON
     *
     * @return JSON status string, or empty string if not set
     */
    static std::string getStatus();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set HTTP server port (default: 80)
     *
     * Must be called before init()
     *
     * @param port Port number (1-65535)
     */
    static void setPort(uint16_t port);

    /**
     * @brief Get current HTTP server port
     *
     * @return Port number
     */
    static uint16_t getPort();

private:
    // ========================================================================
    // Static State
    // ========================================================================

    static AsyncWebServer* server;
    static AsyncWebSocket* ws;
    static bool running;
    static uint16_t port;
    static std::string cachedStatus;

    // ========================================================================
    // Request Handlers
    // ========================================================================

    /**
     * @brief Handler for GET /
     * Returns simple HTML page or redirects to /api/status
     */
    static void handleRoot(AsyncWebServerRequest* request);

    /**
     * @brief Handler for GET /api/status
     * Returns device status in JSON format
     */
    static void handleStatus(AsyncWebServerRequest* request);

    /**
     * @brief Handler for POST /api/relay
     * Controls relay (ON/OFF with optional duration)
     *
     * Expected request body:
     * {
     *   "action": "ON" or "OFF",
     *   "duration_ms": 5000  // optional
     * }
     */
    static void handleRelay(AsyncWebServerRequest* request);

    /**
     * @brief Handler for POST /api/command
     * Sends command to device
     *
     * Expected request body:
     * {
     *   "cmd": "SETWIFI",
     *   "params": {"ssid": "Network", "pass": "password"}
     * }
     */
    static void handleCommand(AsyncWebServerRequest* request);

    /**
     * @brief Handler for GET /api/ota
     * Check for OTA updates (future implementation)
     */
    static void handleOTACheck(AsyncWebServerRequest* request);

    /**
     * @brief Handler for POST /api/ota/update
     * Trigger OTA firmware update (future implementation)
     */
    static void handleOTAUpdate(AsyncWebServerRequest* request);

    /**
     * @brief Provisioning & Configuration (Phase 3)
     */
    static void handleVersion(AsyncWebServerRequest* request);
    static void handleConfig(AsyncWebServerRequest* request);
    static void handleProvision(AsyncWebServerRequest* request);
    static void handleReboot(AsyncWebServerRequest* request);

    /**
     * @brief Handlers for Product Management (V1 Parity)
     */
    static void handleListProducts(AsyncWebServerRequest* request);
    static void handleLoadProduct(AsyncWebServerRequest* request);
    static void handleSaveProduct(AsyncWebServerRequest* request);

    /**
     * @brief Handler for 404 Not Found
     * Called for unrecognized routes
     */
    static void handle404(AsyncWebServerRequest* request);

    /**
     * @brief Add CORS headers to response
     */
    static void addCORSHeaders(AsyncWebServerRequest* request);

    static void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    static void _handleOTAUpdateBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
};
