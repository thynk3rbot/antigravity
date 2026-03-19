/**
 * @file wifi_transport.cpp
 * @brief WiFi transport layer implementation
 */

#include "wifi_transport.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_event.h>
#include <mdns.h>

// ============================================================================
// Static Member Initialization
// ============================================================================

bool WiFiTransport::connected = false;
std::string WiFiTransport::currentIP = "";
std::string WiFiTransport::hostname = "loralink";
uint32_t WiFiTransport::lastReconnectTime = 0;
uint8_t WiFiTransport::reconnectAttempts = 0;
uint8_t WiFiTransport::reconnectBackoffSeconds = 5;
wifi_event_t WiFiTransport::lastWiFiEvent = WIFI_EVENT_STA_STOP;
int WiFiTransport::lastError = 0;

// ============================================================================
// Public Static Methods
// ============================================================================

bool WiFiTransport::init(const std::string& ssid, const std::string& password,
                         const std::string& mdnsHostname) {
  Serial.println("[WiFi] Initializing WiFi transport...");

  hostname = mdnsHostname;

  // WiFi hardware is already initialized by Arduino framework
  // No need to explicitly initialize

  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);

  // Register WiFi event handler
  if (esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WiFiTransport::onWiFiEvent,
        nullptr,
        nullptr) != ESP_OK) {
    Serial.println("[WiFi] ERROR: Failed to register WiFi event handler");
    lastError = -2;
    return false;
  }

  if (esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &WiFiTransport::onWiFiEvent,
        nullptr,
        nullptr) != ESP_OK) {
    Serial.println("[WiFi] ERROR: Failed to register IP event handler");
    lastError = -3;
    return false;
  }

  // Configure and connect to WiFi
  WiFi.begin(ssid.c_str(), password.c_str());

  // Initialize mDNS
  if (!mdns_init()) {
    if (mdns_hostname_set(hostname.c_str()) != ESP_OK) {
      Serial.printf("[WiFi] Warning: Failed to set mDNS hostname: %s\n",
                    hostname.c_str());
      // Non-fatal, continue without mDNS
    } else {
      Serial.printf("[WiFi] mDNS hostname set to: %s.local\n", hostname.c_str());
    }
  } else {
    Serial.println("[WiFi] Warning: mDNS already initialized");
  }

  Serial.printf("[WiFi] Connecting to SSID: %s\n", ssid.c_str());
  Serial.println("[WiFi] WiFi initialization complete");

  reconnectAttempts = 0;
  reconnectBackoffSeconds = 5;
  lastReconnectTime = millis();

  return true;
}

bool WiFiTransport::isConnected() {
  return connected && WiFi.status() == WL_CONNECTED;
}

std::string WiFiTransport::getIP() {
  if (isConnected()) {
    return WiFi.localIP().toString().c_str();
  }
  return "";
}

void WiFiTransport::reconnect() {
  Serial.println("[WiFi] Manual reconnect requested");
  WiFi.begin();
  reconnectAttempts = 0;
  reconnectBackoffSeconds = 5;
  lastReconnectTime = millis();
}

const char* WiFiTransport::getStatusString() {
  wl_status_t status = WiFi.status();
  switch (status) {
    case WL_CONNECTED:
      return "Connected";
    case WL_IDLE_STATUS:
      return "Idle";
    case WL_NO_SSID_AVAIL:
      return "NoSSID";
    case WL_SCAN_COMPLETED:
      return "ScanCompleted";
    case WL_CONNECT_FAILED:
      return "ConnectFailed";
    case WL_CONNECTION_LOST:
      return "ConnectionLost";
    case WL_DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

int8_t WiFiTransport::getWiFiSignalStrength() {
  if (isConnected()) {
    return WiFi.RSSI();
  }
  return 0;
}

uint8_t WiFiTransport::getReconnectAttempts() {
  return reconnectAttempts;
}

// ============================================================================
// TransportInterface Implementation
// ============================================================================

bool WiFiTransport::init() {
  // Placeholder - static init should be called instead
  return isConnected();
}

bool WiFiTransport::isReady() const {
  return connected;
}

int WiFiTransport::send(const uint8_t* payload, size_t len) {
  // Placeholder - WiFi transport uses HTTP API instead
  (void)payload;
  (void)len;
  return -1;  // Not implemented
}

int WiFiTransport::recv(uint8_t* buffer, size_t maxLen) {
  // Placeholder - WiFi transport uses HTTP API instead
  (void)buffer;
  (void)maxLen;
  return 0;  // No data available
}

void WiFiTransport::poll() {
  // Check if reconnection is needed
  if (!isConnected()) {
    uint32_t now = millis();
    uint32_t timeSinceLastAttempt = now - lastReconnectTime;

    if (timeSinceLastAttempt >= (uint32_t)reconnectBackoffSeconds * 1000) {
      Serial.printf("[WiFi] Attempting reconnect (attempt %u, backoff %us)\n",
                    reconnectAttempts + 1, reconnectBackoffSeconds);
      WiFi.begin();
      lastReconnectTime = now;
      scheduleReconnect();
    }
  }
}

const char* WiFiTransport::getLastErrorString() const {
  switch (lastError) {
    case 0:
      return "No error";
    case -1:
      return "WiFi hardware init failed";
    case -2:
      return "WiFi event handler registration failed";
    case -3:
      return "IP event handler registration failed";
    case -4:
      return "Connection timeout";
    default:
      return "Unknown error";
  }
}

// ============================================================================
// WiFi Event Handler
// ============================================================================

void WiFiTransport::onWiFiEvent(void* arg, esp_event_base_t eventBase,
                                int32_t eventId, void* eventData) {
  (void)arg;

  if (eventBase == WIFI_EVENT) {
    switch (eventId) {
      case WIFI_EVENT_STA_START:
        Serial.println("[WiFi] STA mode started");
        lastWiFiEvent = WIFI_EVENT_STA_START;
        break;

      case WIFI_EVENT_STA_CONNECTED:
        Serial.println("[WiFi] Connected to AP");
        lastWiFiEvent = WIFI_EVENT_STA_CONNECTED;
        reconnectBackoffSeconds = 5;  // Reset backoff on success
        break;

      case WIFI_EVENT_STA_DISCONNECTED: {
        connected = false;
        currentIP = "";
        lastWiFiEvent = WIFI_EVENT_STA_DISCONNECTED;

        wifi_event_sta_disconnected_t* disconnect =
            (wifi_event_sta_disconnected_t*)eventData;
        Serial.printf("[WiFi] Disconnected (reason: %u)\n", disconnect->reason);

        scheduleReconnect();
        break;
      }

      case WIFI_EVENT_STA_STOP:
        Serial.println("[WiFi] STA mode stopped");
        connected = false;
        currentIP = "";
        lastWiFiEvent = WIFI_EVENT_STA_STOP;
        break;

      default:
        Serial.printf("[WiFi] Unknown WiFi event: %d\n", (int)eventId);
        break;
    }
  } else if (eventBase == IP_EVENT) {
    if (eventId == IP_EVENT_STA_GOT_IP) {
      currentIP = WiFi.localIP().toString().c_str();
      connected = true;
      reconnectAttempts = 0;
      reconnectBackoffSeconds = 5;

      Serial.printf("[WiFi] IP Address: %s\n", currentIP.c_str());
      Serial.printf("[WiFi] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
    }
  }
}

uint8_t WiFiTransport::getNextBackoffTime() {
  // Exponential backoff: 5, 10, 20, max 60 seconds
  if (reconnectBackoffSeconds < 60) {
    reconnectBackoffSeconds = (reconnectBackoffSeconds >= 30) ? 60
                              : (reconnectBackoffSeconds * 2);
  }
  return reconnectBackoffSeconds;
}

void WiFiTransport::scheduleReconnect() {
  reconnectAttempts++;
  uint8_t nextBackoff = getNextBackoffTime();

  Serial.printf("[WiFi] Will retry in %u seconds (attempt %u/%u)\n",
                nextBackoff, reconnectAttempts, 255);

  lastReconnectTime = millis();
}
