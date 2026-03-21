/**
 * @file wifi_transport.cpp
 * @brief WiFi Transport Layer – Implementation
 *
 * Provides:
 *   - WiFi STA connection with 10-second initial timeout
 *   - Auto-reconnect every 30 seconds on disconnect
 *   - ArduinoOTA on hostname loralink-{nodeId}
 *   - mDNS advertisement as {hostname}.local
 *
 * HTTP REST API endpoints (GET /api/status, POST /api/relay, POST /api/config,
 * GET /, POST /api/reboot) live in lib/App/http_api.cpp and are started
 * separately by main.cpp via HttpAPI::init().
 */

#include "wifi_transport.h"
#include <Arduino.h>

// ============================================================================
// Static Member Initialisation
// ============================================================================

std::string WiFiTransport::_ssid                  = "";
std::string WiFiTransport::_password              = "";
std::string WiFiTransport::_hostname              = "loralink";
bool        WiFiTransport::_otaStarted            = false;
bool        WiFiTransport::_mdnsStarted           = false;
uint32_t    WiFiTransport::_lastReconnectAttempt  = 0;
uint8_t     WiFiTransport::_reconnectAttempts     = 0;
int         WiFiTransport::_lastError             = 0;

// ============================================================================
// Public Static Lifecycle
// ============================================================================

bool WiFiTransport::init(const std::string& ssid,
                          const std::string& password,
                          const std::string& mdnsHostname) {
    Serial.println("[WiFi] Initialising WiFi transport...");

    _ssid     = ssid;
    _password = password;
    _hostname = mdnsHostname;
    _lastError = 0;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);     // let ESP32 reconnect automatically
    WiFi.persistent(false);          // don't write credentials to flash

    bool connected = _connect(WIFI_CONNECT_TIMEOUT_MS);

    if (connected) {
        Serial.printf("[WiFi] Connected — IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        _startOTA();
        _startMDNS();
    } else {
        Serial.printf("[WiFi] Not connected after %u ms — will retry in loop\n",
                      WIFI_CONNECT_TIMEOUT_MS);
        _lastError = -4;  // timeout
    }

    _lastReconnectAttempt = millis();
    return connected;
}

void WiFiTransport::service() {
    // Drive OTA processing (must be called in every loop/task iteration)
    if (_otaStarted) {
        ArduinoOTA.handle();
    }

    // Reconnect if disconnected and enough time has elapsed
    if (WiFi.status() != WL_CONNECTED) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
            _reconnectAttempts++;
            Serial.printf("[WiFi] Reconnect attempt %u (SSID: %s)...\n",
                          _reconnectAttempts, _ssid.c_str());
            _lastReconnectAttempt = now;

            WiFi.disconnect(false);
            if (_connect(WIFI_CONNECT_TIMEOUT_MS)) {
                Serial.printf("[WiFi] Reconnected — IP: %s\n",
                              WiFi.localIP().toString().c_str());
                // Re-start OTA / mDNS if not already running
                if (!_otaStarted)  _startOTA();
                if (!_mdnsStarted) _startMDNS();
                _lastError = 0;
            } else {
                Serial.println("[WiFi] Reconnect failed — will retry");
                _lastError = -4;
            }
        }
    }
}

bool WiFiTransport::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

std::string WiFiTransport::getIP() {
    if (isConnected()) {
        return std::string(WiFi.localIP().toString().c_str());
    }
    return "";
}

std::string WiFiTransport::getSSID() {
    return _ssid;
}

int8_t WiFiTransport::getWiFiSignalStrength() {
    if (isConnected()) {
        return static_cast<int8_t>(WiFi.RSSI());
    }
    return 0;
}

uint8_t WiFiTransport::getReconnectAttempts() {
    return _reconnectAttempts;
}

void WiFiTransport::reconnect() {
    Serial.println("[WiFi] Manual reconnect requested");
    _reconnectAttempts = 0;
    _lastReconnectAttempt = 0;  // force immediate retry on next poll()
}

const char* WiFiTransport::getStatusString() {
    switch (WiFi.status()) {
        case WL_CONNECTED:      return "Connected";
        case WL_IDLE_STATUS:    return "Idle";
        case WL_NO_SSID_AVAIL:  return "NoSSID";
        case WL_CONNECT_FAILED: return "ConnectFailed";
        case WL_CONNECTION_LOST:return "ConnectionLost";
        case WL_DISCONNECTED:   return "Disconnected";
        default:                return "Unknown";
    }
}

// ============================================================================
// TransportInterface Virtual Methods
// ============================================================================

bool WiFiTransport::init() {
    // Instance init() delegates to static isConnected()
    // (real init is done via static WiFiTransport::init(ssid, pass, hostname))
    return isConnected();
}

bool WiFiTransport::isReady() const {
    return isConnected();
}

int WiFiTransport::send(const uint8_t* payload, size_t len) {
    // WiFi transport does not do P2P messaging — commands arrive via HTTP API
    (void)payload;
    (void)len;
    return static_cast<int>(TransportStatus::NOT_READY);
}

int WiFiTransport::recv(uint8_t* buffer, size_t maxLen) {
    // WiFi transport does not maintain a receive queue
    (void)buffer;
    (void)maxLen;
    return 0;
}

void WiFiTransport::poll() {
    // Instance poll() delegates to static service()
    WiFiTransport::service();
}

const char* WiFiTransport::getLastErrorString() const {
    switch (_lastError) {
        case  0:  return "No error";
        case -1:  return "WiFi hardware init failed";
        case -2:  return "WiFi event handler registration failed";
        case -3:  return "IP event handler registration failed";
        case -4:  return "Connection timeout";
        default:  return "Unknown error";
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

bool WiFiTransport::_connect(uint32_t timeoutMs) {
    WiFi.begin(_ssid.c_str(), _password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= timeoutMs) {
            return false;
        }
        delay(WIFI_POLL_INTERVAL_MS);
        Serial.print(".");
    }
    Serial.println();
    return true;
}

void WiFiTransport::_startOTA() {
    if (_otaStarted) return;

    // Hostname shown in PlatformIO OTA targets: loralink-{nodeId}.local
    ArduinoOTA.setHostname(_hostname.c_str());

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.printf("[OTA] Update started — type: %s\n", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update complete");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* errStr = "Unknown";
        switch (error) {
            case OTA_AUTH_ERROR:    errStr = "Auth Failed";    break;
            case OTA_BEGIN_ERROR:   errStr = "Begin Failed";   break;
            case OTA_CONNECT_ERROR: errStr = "Connect Failed"; break;
            case OTA_RECEIVE_ERROR: errStr = "Receive Failed"; break;
            case OTA_END_ERROR:     errStr = "End Failed";     break;
        }
        Serial.printf("[OTA] Error[%u]: %s\n", error, errStr);
    });

    ArduinoOTA.begin();
    _otaStarted = true;
    Serial.printf("[OTA] ArduinoOTA started — hostname: %s.local\n", _hostname.c_str());
}

void WiFiTransport::_startMDNS() {
    if (_mdnsStarted) return;

    if (!MDNS.begin(_hostname.c_str())) {
        Serial.printf("[mDNS] Failed to start for hostname: %s\n", _hostname.c_str());
        return;
    }

    // Advertise HTTP service so network browsers can discover the device
    MDNS.addService("http", "tcp", 80);
    
    // Add TXT records for LoRaLink discovery
    // These allow the webapp to identify the device without an HTTP probe
    MDNS.addServiceTxt("http", "tcp", "id", _hostname.c_str());
    MDNS.addServiceTxt("http", "tcp", "type", "loralink-gateway");
    MDNS.addServiceTxt("http", "tcp", "ver", "v2.0.0");

    _mdnsStarted = true;
    Serial.printf("[mDNS] Advertised as %s.local with TXT records\n", _hostname.c_str());
}
