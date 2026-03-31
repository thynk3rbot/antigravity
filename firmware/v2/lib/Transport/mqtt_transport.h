/**
 * @file mqtt_transport.h
 * @brief MQTT Transport Layer for Magic v2
 *
 * Implements MQTT client for publishing device telemetry and subscribing to
 * commands. Uses PubSubClient library over WiFiClient.
 *
 * Topics (v0.1.0 convention):
 * - Publish:   magic/{nodeId}/telemetry   (retain=true)
 * - Publish:   magic/{nodeId}/response
 * - Subscribe: magic/{nodeId}/command
 * - Subscribe: magic/broadcast/command
 */

#pragma once

#include <Arduino.h>
#include <string>
#include <cstdint>
#include "interface.h"
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <functional>
#include <map>

// ============================================================================
// MQTT Configuration
// ============================================================================

struct MQTTConfig {
    String broker;
    uint16_t port;
    String username;
    String password;
    String clientId;
};

// ============================================================================
// MQTTTransport Class
// ============================================================================

/**
 * @class MQTTTransport
 * @brief MQTT client transport for Magic telemetry and command handling
 *
 * Inherits from TransportInterface. MQTT communication is optional and
 * gracefully degrades if the broker is unavailable or unconfigured.
 *
 * send() / recv() are stubs — MQTT uses publish/subscribe rather than
 * point-to-point framing, so LoRa packet routing does not apply here.
 *
 * Usage:
 *   MQTTTransport mqtt;
 *   mqtt.init();           // load config from NVS, connect
 *   mqtt.setCommandCallback(...);
 *   // in loop:
 *   mqtt.poll();
 *   mqtt.publishTelemetry("{...}");
 */
class MQTTTransport : public TransportInterface {
public:
    MQTTTransport();

    // ========================================================================
    // TransportInterface overrides
    // ========================================================================

    bool init() override;
    void shutdown() override;

    /**
     * PubSubClient::connected() is not const, so isReady() cannot be const.
     * We still satisfy the virtual signature via override.
     */
    bool isReady() const override;

    /** Not used for LoRa packet routing — always returns 0. */
    int send(const uint8_t* payload, size_t len) override;

    /** Not used for LoRa packet routing — always returns 0. */
    int recv(uint8_t* buffer, size_t maxLen) override;

    /** Always returns false — MQTT uses callbacks, not polled recv. */
    bool isAvailable() const override;

    /**
     * @brief Drive the MQTT client state machine
     *
     * - If disconnected and reconnect interval elapsed: attempt _connect()
     * - Calls _mqttClient.loop() when connected
     * Must be called frequently (every loop iteration or at least every 100 ms).
     */
    void poll() override;

    const char* getName() const override { return "MQTT"; }
    TransportType getType() const override { return TransportType::MQTT; }
    const char* getStatus() const override;

    // ========================================================================
    // MQTT-specific API
    // ========================================================================

    /**
     * @brief Override broker configuration (optional; otherwise loaded from NVS)
     *
     * Can be called before or after init(). If called after init() while
     * connected, the current connection is dropped and re-established.
     */
    void configure(const MQTTConfig& config);

    /**
     * @brief Publish a telemetry JSON payload (retained)
     *
     * Topic: magic/{nodeId}/telemetry
     *
     * @param jsonPayload  Fully-formed JSON string
     * @return true if broker accepted the publish
     */
    bool publishTelemetry(const String& jsonPayload);

    /**
     * @brief Publish a command response
     *
     * Topic: magic/{nodeId}/response
     *
     * @param jsonPayload  Response JSON string
     * @return true if broker accepted the publish
     */
    bool publishResponse(const String& jsonPayload);

    /**
     * @brief Publish ONLINE/OFFLINE status for an external Mesh Node
     *
     * Topic: magic/status/{nodeId}
     *
     * @param nodeId Node ID
     * @param isOnline True if node joined, false if it timed out
     * @return true if broker accepted the publish
     */
    bool publishNodeStatus(uint8_t nodeId, bool isOnline);

    /**
     * @brief Register callback invoked when a command arrives
     *
     * The callback receives the raw payload string from either
     * magic/{nodeId}/command or magic/broadcast/command.
     */
    using CommandCallback = std::function<void(const String& cmd)>;
    void setCommandCallback(CommandCallback cb);

    /**
     * @brief Register a custom topic and callback (for plugins)
     */
    using TopicCallback = std::function<void(const String& topic, const String& payload)>;
    void registerTopic(const String& topic, TopicCallback cb);

    /** @return true if currently connected to the broker */
    bool isConnected() const;

    /** @return Node ID used in topic construction */
    String getNodeId() const { return _nodeId; }

    // ========================================================================
    // Static singleton helpers (for main.cpp compatibility)
    // ========================================================================
    static bool initStatic();
    static void pollStatic();
    static void onCommand(CommandCallback cb);
    static void onCommand(std::function<void(const std::string&)> cb);
    static MQTTTransport* instance();

private:
    // ========================================================================
    // Internal helpers
    // ========================================================================

    /**
     * @brief Set server and attempt broker connection
     * @return true on successful connect
     */
    bool _connect();

    /** Subscribe to device and broadcast command topics */
    void _subscribe();

    /** Build topic strings */
    String _telemetryTopic() const;
    String _commandTopic() const;
    String _responseTopic() const;
    String _statusTopic() const;

    // ========================================================================
    // Static callback (PubSubClient requires a plain function pointer)
    // ========================================================================

    static void _mqttCallback(char* topic, byte* payload, unsigned int length);

    /** Singleton pointer used by the static callback to reach the instance */
    static MQTTTransport* _instance;

    // ========================================================================
    // Member data
    // ========================================================================

    WiFiClient    _wifiClient;
    PubSubClient  _mqttClient;
    MQTTConfig    _config;
    CommandCallback _commandCallback;
    std::map<String, TopicCallback> _topicRegistry;

    bool     _configured;           ///< true once a non-empty broker is known
    uint32_t _lastReconnectAttempt; ///< millis() timestamp of last attempt
    String   _nodeId;               ///< loaded from NVSConfig

    static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000;
    static constexpr uint16_t DEFAULT_PORT          = 1883;
};
