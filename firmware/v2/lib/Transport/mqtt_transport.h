/**
 * @file mqtt_transport.h
 * @brief MQTT Transport Layer for LoRaLink v2
 *
 * Implements MQTT client for publishing device telemetry and subscribing to commands.
 * Uses PubSubClient library for broker communication.
 *
 * Features:
 * - Auto-connect to configured MQTT broker
 * - Telemetry publishing (battery, signals, heap, uptime)
 * - Command subscription and callback mechanism
 * - Automatic reconnection with exponential backoff
 * - Graceful fallback if broker unavailable (non-critical transport)
 *
 * Topics:
 * - Publish: loralink/{nodeID}/telemetry (30-second interval)
 * - Publish: loralink/{nodeID}/status (on demand)
 * - Subscribe: loralink/{nodeID}/command (device-specific commands)
 * - Subscribe: loralink/broadcast/command (broadcast commands)
 */

#pragma once

#include "interface.h"
#include <string>
#include <cstdint>
#include <functional>
#include <queue>

// Forward declarations
class PubSubClient;
class WiFiClient;

/**
 * @class MQTTTransport
 * @brief MQTT client transport for LoRaLink telemetry and command handling
 *
 * Thread-safe singleton using static methods (not a TransportInterface implementation).
 * MQTT communication is optional and gracefully degrades if broker unavailable.
 *
 * Note: MQTTTransport does not inherit from TransportInterface because it uses
 * a different communication pattern (callback-based for commands, publish-based for telemetry)
 * rather than send/recv. However, it provides similar diagnostic methods.
 */
class MQTTTransport {
public:
    // ========================================================================
    // Initialization & Control
    // ========================================================================

    /**
     * @brief Initialize MQTT client and connect to broker
     *
     * Reads broker address/port from NVSManager.
     * If broker not configured, initialization succeeds but connection is skipped.
     *
     * Must be called after WiFi transport is initialized.
     *
     * @return true if initialization successful (or skipped due to no config),
     *         false on critical error
     */
    static bool init();

    /**
     * @brief Check if connected to MQTT broker
     * @return true if currently connected, false otherwise
     */
    static bool isConnected();

    /**
     * @brief Poll MQTT client
     *
     * Must be called frequently (at least every 100-500ms).
     * Handles:
     * - Reconnection attempts
     * - Message receipt and callback invocation
     * - Automatic reconnection with exponential backoff
     *
     * Non-blocking; safe to call from any task.
     */
    static void poll();

    /**
     * @brief Gracefully disconnect and shutdown MQTT
     */
    static void shutdown();

    /**
     * @brief Get connection status as string
     * @return Status: "Connected", "Connecting", "Disconnected", "Not configured"
     */
    static const char* getStatusString();

    // ========================================================================
    // Publishing
    // ========================================================================

    /**
     * @brief Publish telemetry data
     *
     * Gathers:
     * - Battery voltage and percentage (from PowerManager)
     * - Power mode (from PowerManager)
     * - LoRa RSSI/SNR (from last packet)
     * - WiFi RSSI (if connected)
     * - Free heap (esp_get_free_heap_size)
     * - Uptime (millis())
     * - Peer count (from MeshCoordinator)
     *
     * Published to: loralink/{nodeID}/telemetry
     *
     * @return true if published successfully, false on error
     */
    static bool publishTelemetry();

    /**
     * @brief Publish device status
     *
     * Publishes full device status JSON (typically from /api/status endpoint).
     *
     * Topic: loralink/{nodeID}/status
     *
     * @param statusJson JSON string with device status
     * @return true if published successfully, false on error
     */
    static bool publishStatus(const std::string& statusJson);

    /**
     * @brief Publish custom message to arbitrary topic
     *
     * @param topic MQTT topic (e.g., "loralink/custom")
     * @param payload Message payload (plain text or JSON)
     * @return true if published successfully, false on error
     */
    static bool publish(const std::string& topic, const std::string& payload);

    // ========================================================================
    // Command Subscription & Callbacks
    // ========================================================================

    /**
     * @brief Command callback type
     *
     * Called when a command is received on subscribed topics.
     * Payload is plain text command (e.g., "STATUS", "RELAY:ON:0").
     */
    typedef std::function<void(const std::string& command)> CommandCallback;

    /**
     * @brief Register callback for received commands
     *
     * Callback is invoked when message arrives on:
     * - loralink/{nodeID}/command (device-specific)
     * - loralink/broadcast/command (broadcast to all devices)
     *
     * @param callback Function to call with command text
     */
    static void onCommand(CommandCallback callback);

    // ========================================================================
    // Diagnostics & Statistics
    // ========================================================================

    /**
     * @brief Get broker address
     * @return Broker hostname/IP, or empty string if not configured
     */
    static std::string getBrokerAddress();

    /**
     * @brief Get broker port
     * @return Port number (typically 1883)
     */
    static uint16_t getBrokerPort();

    /**
     * @brief Get number of reconnection attempts
     * @return Count of failed connection attempts
     */
    static uint8_t getReconnectAttempts();

    /**
     * @brief Get bytes published since init
     * @return Total bytes sent via MQTT
     */
    static uint32_t getTxBytes();

    /**
     * @brief Get bytes received since init
     * @return Total bytes received via MQTT
     */
    static uint32_t getRxBytes();

    /**
     * @brief Get last error code
     * @return Error code (0 = no error)
     */
    static int getLastError();

    /**
     * @brief Get human-readable diagnostic status
     * @return Status string with connection info, statistics
     */
    static std::string getDiagnostics();

private:
    // ========================================================================
    // Configuration & State
    // ========================================================================

    // Broker configuration
    static std::string brokerAddress;
    static uint16_t brokerPort;
    static bool brokerConfigured;

    // Connection state
    static bool initialized;
    static bool connected;
    static uint32_t lastConnectAttempt;
    static uint8_t reconnectAttempts;
    static uint8_t reconnectBackoffSeconds;
    static const uint8_t MAX_RECONNECT_ATTEMPTS;
    static const uint8_t MAX_BACKOFF_SECONDS;

    // Node ID for topic naming
    static std::string nodeID;

    // Statistics
    static uint32_t txBytes;
    static uint32_t rxBytes;
    static int lastErrorCode;
    static const char* lastErrorMessage;

    // Callbacks & message queue
    static CommandCallback commandCallback;
    static std::queue<std::string> commandQueue;
    static const size_t MAX_COMMAND_QUEUE_SIZE;

    // Telemetry timing
    static uint32_t lastTelemetryTime;
    static const uint32_t MQTT_TELEMETRY_INTERVAL_MS;

    // MQTT client instances (managed internally)
    static WiFiClient* wifiClient;
    static PubSubClient* mqttClient;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Internal method to establish broker connection
     * @return true if connected successfully, false otherwise
     */
    static bool connect();

    /**
     * @brief Internal method to handle incoming MQTT messages
     * Called by PubSubClient message callback.
     */
    static void onMQTTMessage(char* topic, uint8_t* payload, unsigned int length);

    /**
     * @brief Schedule reconnection with exponential backoff
     */
    static void scheduleReconnect();

    /**
     * @brief Get next backoff time (exponential)
     * @return Seconds to wait before next reconnect attempt
     */
    static uint8_t getNextBackoffTime();

    /**
     * @brief Subscribe to command topics
     * @return true if subscription successful, false otherwise
     */
    static bool subscribeToCommands();

    /**
     * @brief Process queued commands (call from poll or main loop)
     */
    static void processCommandQueue();

    /**
     * @brief Static wrapper for PubSubClient callback
     * (MQTT callback must be a C function pointer, so we use a static method)
     */
    friend void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length);

    // Private constructor (singleton)
    MQTTTransport();
};
