/**
 * @file mqtt_transport.cpp
 * @brief MQTT Transport Layer Implementation for LoRaLink v2
 *
 * PubSubClient-based MQTT over WiFi.
 * Topics (v0.1.0 convention):
 *   loralink/{nodeId}/telemetry   — device publishes (retain=true)
 *   loralink/{nodeId}/command     — device subscribes
 *   loralink/{nodeId}/response    — device publishes responses
 *   loralink/broadcast/command    — device subscribes (broadcast)
 */

#include "mqtt_transport.h"
#include <Preferences.h>
#include <WiFi.h>

// NVS key constants — mirrored from nvs_config.h to avoid cross-lib include
// (Preferences.h is an Arduino framework library not always auto-included
// when nvs_config.h is pulled into a different PlatformIO lib context)
#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "loralink"
#endif
static const char* _kNodeId     = "node_id";
static const char* _kMqttBroker = "mqtt_broker";
static const char* _kMqttPort   = "mqtt_port";
static const char* _kMqttUser   = "mqtt_user";
static const char* _kMqttPass   = "mqtt_pass";

// ============================================================================
// Static member initialisation
// ============================================================================

MQTTTransport* MQTTTransport::_instance = nullptr;

// ============================================================================
// Constructor
// ============================================================================

MQTTTransport::MQTTTransport()
    : _mqttClient(_wifiClient),
      _configured(false),
      _lastReconnectAttempt(0)
{
    _instance = this;
}

// ============================================================================
// TransportInterface — init / shutdown / isReady
// ============================================================================

bool MQTTTransport::init() {
    Serial.println("[MQTT] Initializing...");

    // Load config directly from NVS (Preferences) to avoid cross-lib include
    {
        Preferences prefs;
        if (prefs.begin(NVS_NAMESPACE, true)) {
            // Node ID — generate from MAC if not set
            _nodeId = prefs.getString(_kNodeId, "");
            if (_nodeId.length() == 0) {
                // Derive from MAC last 3 bytes
                String mac = WiFi.macAddress();
                String suffix = mac.substring(9);
                suffix.replace(":", "");
                suffix.toUpperCase();
                _nodeId = "NODE_" + suffix;
            }
            _config.broker   = prefs.getString(_kMqttBroker, "");
            _config.port     = prefs.getUShort(_kMqttPort, 1883);
            _config.username = prefs.getString(_kMqttUser, "");
            _config.password = prefs.getString(_kMqttPass, "");
            prefs.end();
        } else {
            Serial.println("[MQTT] WARNING: Could not open NVS — using defaults");
            _nodeId = "NODE_000000";
            _config.port = 1883;
        }
    }
    _config.clientId = "loralink-" + _nodeId;

    if (_config.broker.length() == 0) {
        Serial.println("[MQTT] No broker configured — MQTT disabled");
        _configured = false;
        return false;
    }

    _configured = true;

    // Wire up callback
    _mqttClient.setCallback(_mqttCallback);

    Serial.printf("[MQTT] Broker: %s:%u, clientId: %s\n",
                  _config.broker.c_str(), _config.port, _config.clientId.c_str());

    // Attempt initial connection (failure is non-fatal — poll() will retry)
    if (!_connect()) {
        Serial.println("[MQTT] Initial connection failed — will retry every 30 s");
    }

    return true;
}

void MQTTTransport::shutdown() {
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }
    _configured = false;
    Serial.println("[MQTT] Shutdown");
}

bool MQTTTransport::isReady() const {
    // PubSubClient::connected() is not const — use const_cast
    return _configured &&
           const_cast<PubSubClient&>(_mqttClient).connected();
}

bool MQTTTransport::isConnected() const {
    return const_cast<PubSubClient&>(_mqttClient).connected();
}

// ============================================================================
// TransportInterface — send / recv / isAvailable (stubs)
// ============================================================================

int MQTTTransport::send(const uint8_t* /*payload*/, size_t /*len*/) {
    // MQTT uses publish/subscribe — not used for packet routing
    return 0;
}

int MQTTTransport::recv(uint8_t* /*buffer*/, size_t /*maxLen*/) {
    // MQTT uses callbacks — not used for packet routing
    return 0;
}

bool MQTTTransport::isAvailable() const {
    return false;
}

// ============================================================================
// TransportInterface — poll
// ============================================================================

void MQTTTransport::poll() {
    if (!_configured) return;

    if (!_mqttClient.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
            Serial.println("[MQTT] Attempting reconnect...");
            _connect();
        }
    } else {
        _mqttClient.loop();
    }
}

// ============================================================================
// TransportInterface — status
// ============================================================================

const char* MQTTTransport::getStatus() const {
    if (!_configured) return "Not configured";
    // PubSubClient::connected() is not const — use const_cast
    if (const_cast<PubSubClient&>(_mqttClient).connected()) return "Connected";
    return "Disconnected";
}

// ============================================================================
// MQTT-specific API — configure
// ============================================================================

void MQTTTransport::configure(const MQTTConfig& config) {
    _config = config;
    if (_config.clientId.length() == 0) {
        _config.clientId = "loralink-" + _nodeId;
    }

    _configured = (_config.broker.length() > 0);

    // If already connected, drop and reconnect with new config
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }

    if (_configured) {
        _connect();
    }
}

// ============================================================================
// MQTT-specific API — publish
// ============================================================================

bool MQTTTransport::publishTelemetry(const String& jsonPayload) {
    if (!_mqttClient.connected()) return false;

    String topic = _telemetryTopic();
    bool ok = _mqttClient.publish(topic.c_str(),
                                  (const uint8_t*)jsonPayload.c_str(),
                                  jsonPayload.length(),
                                  true /* retain */);
    if (ok) {
        Serial.printf("[MQTT] Telemetry published (%u bytes) → %s\n",
                      jsonPayload.length(), topic.c_str());
    } else {
        Serial.printf("[MQTT] ERROR: Failed to publish telemetry to %s\n",
                      topic.c_str());
    }
    return ok;
}

bool MQTTTransport::publishResponse(const String& jsonPayload) {
    if (!_mqttClient.connected()) return false;

    String topic = _responseTopic();
    bool ok = _mqttClient.publish(topic.c_str(),
                                  (const uint8_t*)jsonPayload.c_str(),
                                  jsonPayload.length(),
                                  false /* not retained */);
    if (ok) {
        Serial.printf("[MQTT] Response published (%u bytes) → %s\n",
                      jsonPayload.length(), topic.c_str());
    } else {
        Serial.printf("[MQTT] ERROR: Failed to publish response to %s\n",
                      topic.c_str());
    }
    return ok;
}

// ============================================================================
// MQTT-specific API — command callback
// ============================================================================

void MQTTTransport::setCommandCallback(CommandCallback cb) {
    _commandCallback = cb;
}

// ============================================================================
// Internal — _connect
// ============================================================================

bool MQTTTransport::_connect() {
    _lastReconnectAttempt = millis();

    // WiFi must be up before MQTT can connect
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi not connected — skipping broker connect");
        return false;
    }

    _mqttClient.setServer(_config.broker.c_str(), _config.port);
    _mqttClient.setCallback(_mqttCallback);

    bool connected = false;
    if (_config.username.length() > 0) {
        connected = _mqttClient.connect(_config.clientId.c_str(),
                                        _config.username.c_str(),
                                        _config.password.c_str());
    } else {
        connected = _mqttClient.connect(_config.clientId.c_str());
    }

    if (connected) {
        Serial.printf("[MQTT] Connected to %s:%u\n",
                      _config.broker.c_str(), _config.port);
        _subscribe();
    } else {
        Serial.printf("[MQTT] Connect failed (state=%d)\n",
                      _mqttClient.state());
    }

    return connected;
}

// ============================================================================
// Internal — _subscribe
// ============================================================================

void MQTTTransport::_subscribe() {
    String deviceCmd = _commandTopic();
    _mqttClient.subscribe(deviceCmd.c_str());
    _mqttClient.subscribe("loralink/broadcast/command");

    Serial.printf("[MQTT] Subscribed: %s\n", deviceCmd.c_str());
    Serial.println("[MQTT] Subscribed: loralink/broadcast/command");
}

// ============================================================================
// Internal — static MQTT callback
// ============================================================================

void MQTTTransport::_mqttCallback(char* topic, byte* payload,
                                   unsigned int length) {
    if (!_instance) return;

    String topicStr(topic);

    // Build payload string (limit to 512 chars to avoid runaway allocation)
    unsigned int limit = (length > 512) ? 512 : length;
    String payloadStr;
    payloadStr.reserve(limit);
    for (unsigned int i = 0; i < limit; i++) {
        payloadStr += (char)payload[i];
    }

    Serial.printf("[MQTT] RX [%s]: %s\n", topic, payloadStr.c_str());

    // Dispatch to command callback if topic is a command topic
    String deviceCmd = _instance->_commandTopic();
    if (topicStr == deviceCmd ||
        topicStr == "loralink/broadcast/command") {
        if (_instance->_commandCallback) {
            _instance->_commandCallback(payloadStr);
        }
    }
}

// ============================================================================
// Internal — topic helpers
// ============================================================================

String MQTTTransport::_telemetryTopic() const {
    return "loralink/" + _nodeId + "/telemetry";
}

String MQTTTransport::_commandTopic() const {
    return "loralink/" + _nodeId + "/command";
}

String MQTTTransport::_responseTopic() const {
    return "loralink/" + _nodeId + "/response";
}
