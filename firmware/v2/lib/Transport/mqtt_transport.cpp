/**
 * @file mqtt_transport.cpp
 * @brief MQTT Transport Layer Implementation
 *
 * Implements MQTT client for publishing telemetry and subscribing to commands.
 * Uses PubSubClient library with WiFiClient for ESP32.
 */

#include "mqtt_transport.h"
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Application managers
#include "../App/nvs_manager.h"
#include "../App/power_manager.h"
#include "../App/mesh_coordinator.h"

// ============================================================================
// Static Member Initialization
// ============================================================================

// Configuration
std::string MQTTTransport::brokerAddress = "";
uint16_t MQTTTransport::brokerPort = 1883;
bool MQTTTransport::brokerConfigured = false;

// Connection state
bool MQTTTransport::initialized = false;
bool MQTTTransport::connected = false;
uint32_t MQTTTransport::lastConnectAttempt = 0;
uint8_t MQTTTransport::reconnectAttempts = 0;
uint8_t MQTTTransport::reconnectBackoffSeconds = 1;
const uint8_t MQTTTransport::MAX_RECONNECT_ATTEMPTS = 10;
const uint8_t MQTTTransport::MAX_BACKOFF_SECONDS = 120;

// Node ID
std::string MQTTTransport::nodeID = "Device";

// Statistics
uint32_t MQTTTransport::txBytes = 0;
uint32_t MQTTTransport::rxBytes = 0;
int MQTTTransport::lastErrorCode = 0;
const char* MQTTTransport::lastErrorMessage = "No error";

// Callbacks & queue
MQTTTransport::CommandCallback MQTTTransport::commandCallback = nullptr;
std::queue<std::string> MQTTTransport::commandQueue;
const size_t MQTTTransport::MAX_COMMAND_QUEUE_SIZE = 16;

// Telemetry timing
uint32_t MQTTTransport::lastTelemetryTime = 0;
const uint32_t MQTTTransport::MQTT_TELEMETRY_INTERVAL_MS = 30000;  // 30 seconds

// MQTT client instances
WiFiClient* MQTTTransport::wifiClient = nullptr;
PubSubClient* MQTTTransport::mqttClient = nullptr;

// ============================================================================
// Static Callback Wrapper (required for PubSubClient)
// ============================================================================

/**
 * @brief Static callback function for PubSubClient
 *
 * PubSubClient requires a C-style function pointer, so we use a static wrapper
 * that delegates to the MQTTTransport class.
 */
void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length) {
    MQTTTransport::onMQTTMessage(topic, payload, length);
}

// ============================================================================
// Initialization & Control
// ============================================================================

bool MQTTTransport::init() {
    if (initialized) {
        return true;  // Already initialized
    }

    Serial.println("[MQTT] Initializing MQTT transport...");

    // Read configuration from NVS
    brokerAddress = NVSManager::getMQTTBroker("");
    brokerPort = NVSManager::getMQTTPort(1883);
    nodeID = NVSManager::getNodeID("Device");

    if (brokerAddress.empty()) {
        Serial.println("[MQTT] No broker configured in NVS, MQTT disabled");
        brokerConfigured = false;
        initialized = true;  // Still mark as initialized (graceful degradation)
        return true;
    }

    brokerConfigured = true;
    initialized = true;

    // Create WiFiClient and PubSubClient instances
    wifiClient = new WiFiClient();
    if (!wifiClient) {
        lastErrorCode = -1;
        lastErrorMessage = "Memory allocation failed for WiFiClient";
        Serial.println("[MQTT] ERROR: Failed to allocate WiFiClient");
        return false;
    }

    mqttClient = new PubSubClient(*wifiClient);
    if (!mqttClient) {
        lastErrorCode = -1;
        lastErrorMessage = "Memory allocation failed for PubSubClient";
        Serial.println("[MQTT] ERROR: Failed to allocate PubSubClient");
        delete wifiClient;
        wifiClient = nullptr;
        return false;
    }

    // Configure PubSubClient
    mqttClient->setServer(brokerAddress.c_str(), brokerPort);
    mqttClient->setCallback(mqttMessageCallback);
    mqttClient->setKeepAlive(30);  // 30 second keep-alive
    mqttClient->setSocketTimeout(5);  // 5 second socket timeout

    // Set buffer size for large messages (telemetry + status)
    mqttClient->setBufferSize(512);

    Serial.printf("[MQTT] Configured: broker=%s:%u\n", brokerAddress.c_str(), brokerPort);
    Serial.printf("[MQTT] Client ID: %s-v2\n", nodeID.c_str());

    // Attempt initial connection
    if (connect()) {
        Serial.println("[MQTT] Connected on first attempt");
    } else {
        Serial.println("[MQTT] Initial connection failed, will retry");
    }

    return true;
}

bool MQTTTransport::isConnected() {
    if (!initialized || !brokerConfigured) {
        return false;
    }
    if (!mqttClient) {
        return false;
    }
    return mqttClient->connected();
}

void MQTTTransport::shutdown() {
    if (mqttClient) {
        mqttClient->disconnect();
        delete mqttClient;
        mqttClient = nullptr;
    }
    if (wifiClient) {
        wifiClient->stop();
        delete wifiClient;
        wifiClient = nullptr;
    }
    connected = false;
    initialized = false;
    Serial.println("[MQTT] Shutdown complete");
}

const char* MQTTTransport::getStatusString() {
    if (!initialized || !brokerConfigured) {
        return "Not configured";
    }
    if (!isConnected()) {
        return "Disconnected";
    }
    return "Connected";
}

// ============================================================================
// Publishing
// ============================================================================

bool MQTTTransport::publishTelemetry() {
    if (!isConnected()) {
        return false;
    }

    // Gather telemetry data
    uint32_t now = millis();
    float batteryV = PowerManager::getBatteryVoltage();
    PowerMode mode = PowerManager::getMode();

    // Calculate battery percentage (rough estimate: 2.8V = 0%, 4.2V = 100%)
    uint8_t batteryPct = 0;
    if (batteryV >= 4.2f) {
        batteryPct = 100;
    } else if (batteryV >= 2.8f) {
        batteryPct = static_cast<uint8_t>((batteryV - 2.8f) / (4.2f - 2.8f) * 100.0f);
    }

    // LoRa signal (placeholder - would need to track last packet RSSI)
    int8_t loraRSSI = -100;
    int8_t loraSNR = 0;

    // WiFi signal (placeholder - would need WiFi connection info)
    int8_t wifiRSSI = 0;

    // Build JSON telemetry payload
    StaticJsonDocument<256> doc;
    doc["ts"] = (now / 1000);  // Unix timestamp approximation
    doc["bat_v"] = batteryV;
    doc["bat_pct"] = batteryPct;
    doc["mode"] = (mode == PowerMode::NORMAL ? "NORMAL" :
                   (mode == PowerMode::CONSERVE ? "CONSERVE" : "CRITICAL"));
    doc["lora_rssi"] = loraRSSI;
    doc["lora_snr"] = loraSNR;
    doc["wifi_rssi"] = wifiRSSI;
    doc["heap"] = esp_get_free_heap_size();
    doc["uptime"] = now / 1000;  // Seconds
    doc["peers_count"] = meshCoordinator.getNeighborCount();

    // Serialize JSON
    std::string jsonStr;
    size_t len = serializeJson(doc, jsonStr);

    // Build topic: loralink/{nodeID}/telemetry
    std::string topic = "loralink/" + nodeID + "/telemetry";

    // Publish with QoS 0, non-retained
    bool success = mqttClient->publish(topic.c_str(), (uint8_t*)jsonStr.c_str(),
                                       jsonStr.length(), false);

    if (success) {
        txBytes += jsonStr.length();
        Serial.printf("[MQTT] Published telemetry (%u bytes)\n", jsonStr.length());
    } else {
        lastErrorCode = -2;
        lastErrorMessage = "Publish failed";
        Serial.println("[MQTT] ERROR: Telemetry publish failed");
    }

    return success;
}

bool MQTTTransport::publishStatus(const std::string& statusJson) {
    if (!isConnected()) {
        return false;
    }

    std::string topic = "loralink/" + nodeID + "/status";

    bool success = mqttClient->publish(topic.c_str(), (uint8_t*)statusJson.c_str(),
                                       statusJson.length(), false);

    if (success) {
        txBytes += statusJson.length();
        Serial.printf("[MQTT] Published status (%u bytes)\n", statusJson.length());
    } else {
        lastErrorCode = -2;
        lastErrorMessage = "Publish failed";
        Serial.println("[MQTT] ERROR: Status publish failed");
    }

    return success;
}

bool MQTTTransport::publish(const std::string& topic, const std::string& payload) {
    if (!isConnected()) {
        return false;
    }

    bool success = mqttClient->publish(topic.c_str(), (uint8_t*)payload.c_str(),
                                       payload.length(), false);

    if (success) {
        txBytes += payload.length();
        Serial.printf("[MQTT] Published to %s (%u bytes)\n", topic.c_str(), payload.length());
    } else {
        lastErrorCode = -2;
        lastErrorMessage = "Publish failed";
    }

    return success;
}

// ============================================================================
// Command Subscription & Callbacks
// ============================================================================

void MQTTTransport::onCommand(CommandCallback callback) {
    commandCallback = callback;
}

void MQTTTransport::onMQTTMessage(char* topic, uint8_t* payload, unsigned int length) {
    if (!topic || !payload || length == 0) {
        return;
    }

    // Track bytes received
    rxBytes += length;

    // Parse topic to identify command type
    std::string topicStr(topic);
    std::string payloadStr(reinterpret_cast<char*>(payload), length);

    Serial.printf("[MQTT] Message on %s: %s\n", topic, payloadStr.c_str());

    // Check if this is a command topic
    if (topicStr.find("/command") != std::string::npos) {
        // Queue command if not too many pending
        if (commandQueue.size() < MAX_COMMAND_QUEUE_SIZE) {
            commandQueue.push(payloadStr);
            Serial.printf("[MQTT] Queued command: %s\n", payloadStr.c_str());
        } else {
            Serial.println("[MQTT] WARNING: Command queue full, dropping message");
        }
    }
}

// ============================================================================
// Polling & Connection Management
// ============================================================================

void MQTTTransport::poll() {
    if (!initialized || !brokerConfigured) {
        return;
    }

    if (!mqttClient) {
        return;
    }

    uint32_t now = millis();

    // Ensure MQTT client connection
    if (!mqttClient->connected()) {
        connected = false;

        // Check if enough time has passed to attempt reconnection
        if (now - lastConnectAttempt >= (reconnectBackoffSeconds * 1000)) {
            if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
                Serial.printf("[MQTT] Attempting reconnect (attempt %u/%u)\n",
                             reconnectAttempts + 1, MAX_RECONNECT_ATTEMPTS);
                connect();
            } else {
                Serial.printf("[MQTT] Max reconnect attempts reached (%u)\n",
                             MAX_RECONNECT_ATTEMPTS);
            }
        }
    } else {
        connected = true;
        reconnectAttempts = 0;  // Reset on successful connection
        reconnectBackoffSeconds = 1;  // Reset backoff

        // Regular MQTT loop (handle subscribed messages)
        mqttClient->loop();

        // Publish telemetry if interval elapsed
        if (now - lastTelemetryTime >= MQTT_TELEMETRY_INTERVAL_MS) {
            publishTelemetry();
            lastTelemetryTime = now;
        }
    }

    // Process any queued commands
    processCommandQueue();
}

bool MQTTTransport::connect() {
    if (!mqttClient || !brokerConfigured) {
        return false;
    }

    lastConnectAttempt = millis();

    // Build client ID: "{nodeID}-v2"
    std::string clientID = nodeID + "-v2";

    Serial.printf("[MQTT] Connecting to %s:%u with client ID '%s'...\n",
                 brokerAddress.c_str(), brokerPort, clientID.c_str());

    // Attempt connection (no username/password for now)
    if (mqttClient->connect(clientID.c_str())) {
        Serial.println("[MQTT] Connected successfully");
        connected = true;
        reconnectAttempts = 0;
        reconnectBackoffSeconds = 1;

        // Subscribe to command topics
        if (!subscribeToCommands()) {
            Serial.println("[MQTT] WARNING: Failed to subscribe to command topics");
        }

        return true;
    } else {
        connected = false;
        reconnectAttempts++;

        int state = mqttClient->state();
        Serial.printf("[MQTT] Connection failed (state=%d), attempt %u/%u\n",
                     state, reconnectAttempts, MAX_RECONNECT_ATTEMPTS);

        // Map MQTT state codes to error messages
        const char* stateMsg = "Unknown error";
        switch (state) {
            case -4: stateMsg = "Connect failed"; break;
            case -3: stateMsg = "Connection lost"; break;
            case -2: stateMsg = "Connect bad protocol"; break;
            case -1: stateMsg = "Not connected"; break;
            case 0:  stateMsg = "Connected"; break;
            case 1:  stateMsg = "Bad client ID"; break;
            case 2:  stateMsg = "Bad credentials"; break;
            case 3:  stateMsg = "Unavailable"; break;
            case 4:  stateMsg = "Unauthorized"; break;
            case 5:  stateMsg = "Server unavailable"; break;
        }
        lastErrorMessage = stateMsg;

        scheduleReconnect();
        return false;
    }
}

bool MQTTTransport::subscribeToCommands() {
    if (!mqttClient || !connected) {
        return false;
    }

    // Subscribe to device-specific commands
    std::string deviceTopic = "loralink/" + nodeID + "/command";
    bool success1 = mqttClient->subscribe(deviceTopic.c_str());

    // Subscribe to broadcast commands
    bool success2 = mqttClient->subscribe("loralink/broadcast/command");

    if (success1 && success2) {
        Serial.printf("[MQTT] Subscribed to command topics\n");
        return true;
    } else {
        Serial.printf("[MQTT] WARNING: Failed to subscribe to all topics (s1=%d, s2=%d)\n",
                     success1, success2);
        return false;
    }
}

void MQTTTransport::processCommandQueue() {
    while (!commandQueue.empty() && commandCallback) {
        std::string command = commandQueue.front();
        commandQueue.pop();

        Serial.printf("[MQTT] Processing command: %s\n", command.c_str());
        commandCallback(command);
    }
}

void MQTTTransport::scheduleReconnect() {
    uint8_t backoff = getNextBackoffTime();
    reconnectBackoffSeconds = backoff;
    Serial.printf("[MQTT] Next reconnect attempt in %u seconds\n", backoff);
}

uint8_t MQTTTransport::getNextBackoffTime() {
    // Exponential backoff: 1, 2, 4, 8, 16, 32, 64, 120, 120, ...
    uint8_t backoff = (1 << reconnectAttempts);  // 2^attempts
    if (backoff > MAX_BACKOFF_SECONDS) {
        backoff = MAX_BACKOFF_SECONDS;
    }
    return backoff;
}

// ============================================================================
// Diagnostics & Statistics
// ============================================================================

std::string MQTTTransport::getBrokerAddress() {
    return brokerAddress;
}

uint16_t MQTTTransport::getBrokerPort() {
    return brokerPort;
}

uint8_t MQTTTransport::getReconnectAttempts() {
    return reconnectAttempts;
}

uint32_t MQTTTransport::getTxBytes() {
    return txBytes;
}

uint32_t MQTTTransport::getRxBytes() {
    return rxBytes;
}

int MQTTTransport::getLastError() {
    return lastErrorCode;
}

std::string MQTTTransport::getDiagnostics() {
    std::string diag = "[MQTT Diagnostics]\n";

    if (!brokerConfigured) {
        diag += "Status: Not configured\n";
        return diag;
    }

    diag += "Broker: " + brokerAddress + ":" + std::to_string(brokerPort) + "\n";
    diag += "Connected: " + std::string(isConnected() ? "Yes" : "No") + "\n";
    diag += "Client ID: " + nodeID + "-v2\n";
    diag += "TX Bytes: " + std::to_string(txBytes) + "\n";
    diag += "RX Bytes: " + std::to_string(rxBytes) + "\n";
    diag += "Reconnect Attempts: " + std::to_string(reconnectAttempts) + "/" +
            std::to_string(MAX_RECONNECT_ATTEMPTS) + "\n";
    diag += "Backoff: " + std::to_string(reconnectBackoffSeconds) + "s\n";
    diag += "Command Queue: " + std::to_string(commandQueue.size()) + "/" +
            std::to_string(MAX_COMMAND_QUEUE_SIZE) + "\n";
    diag += "Last Error: " + std::string(lastErrorMessage) + "\n";

    return diag;
}

