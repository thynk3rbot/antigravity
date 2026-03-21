/**
 * @file espnow_transport.cpp
 * @brief ESP-NOW Transport Implementation
 */

#include "espnow_transport.h"
#include "board_config.h"
#include "control_packet.h"
#include "crypto_hal.h"
#include <cstring>

ESPNowTransport& espNowTransport = ESPNowTransport::getInstance();

ESPNowTransport& ESPNowTransport::getInstance() {
    static ESPNowTransport instance;
    return instance;
}

// Global buffer for received data (queueing not implemented yet)
static uint8_t rxBuffer[256];
static size_t rxLen = 0;
static bool rxFlag = false;

void ESPNowTransport::_onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Optional status tracking
}

void ESPNowTransport::_onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (len > 0 && len <= 250) {
        memcpy(rxBuffer, data, len);
        rxLen = len;
        rxFlag = true;
    }
}

bool ESPNowTransport::init() {
    if (_initialized) return true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        _lastError = -100;
        return false;
    }

    esp_now_register_send_cb(_onDataSent);
    esp_now_register_recv_cb(_onDataRecv);

    // Register broadcast peer
    esp_now_peer_info_t broadcastPeer = {};
    memset(broadcastPeer.peer_addr, 0xFF, 6);
    broadcastPeer.channel = 0;
    broadcastPeer.encrypt = false;
    esp_now_add_peer(&broadcastPeer);

    _initialized = true;
    Serial.println("[ESPNowTransport] Initialized");
    return true;
}

void ESPNowTransport::shutdown() {
    esp_now_deinit();
    _initialized = false;
}

bool ESPNowTransport::isReady() const {
    return _initialized;
}

int ESPNowTransport::send(const uint8_t* payload, size_t len) {
    if (!_initialized) return -1;

    uint8_t txBuffer[256];
    size_t encLen = len;
    memcpy(txBuffer, payload, len);

    if (!_encryptPacket(txBuffer, &encLen)) return -2;

    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t result = esp_now_send(broadcastMac, txBuffer, encLen);
    
    return (result == ESP_OK) ? (int)len : -3;
}

int ESPNowTransport::recv(uint8_t* buffer, size_t maxLen) {
    if (!rxFlag) return 0;

    size_t len = rxLen;
    if (len > maxLen) len = maxLen;

    memcpy(buffer, rxBuffer, len);
    
    size_t decLen = len;
    if (!_decryptPacket(buffer, &decLen)) {
        rxFlag = false;
        return -4;
    }

    rxFlag = false;
    return (int)decLen;
}

bool ESPNowTransport::isAvailable() const {
    return rxFlag;
}

void ESPNowTransport::poll() {
    if (_discoveryActive && (millis() - _lastDiscoveryMs > 5000)) {
        // Broadcast discovery packet (Mesh Probe)
        ControlPacket probe = ControlPacket::makeHeartbeat(0); // Use heartbeat as probe
        send((uint8_t*)&probe, sizeof(probe));
        _lastDiscoveryMs = millis();
        Serial.println("[ESPNowTransport] Discovery broadcast sent");
    }
}

const char* ESPNowTransport::getStatus() const {
    return _initialized ? "READY" : "OFFLINE";
}

int8_t ESPNowTransport::getSignalStrength() const {
    return 0; // RSSI not available directly from esp_now callback Mac header
}

const char* ESPNowTransport::getLastErrorString() const {
    return "None";
}

void ESPNowTransport::startDiscovery() {
    _discoveryActive = true;
    _lastDiscoveryMs = 0;
}

void ESPNowTransport::markDiscovered(const uint8_t* mac) {
    // Logic to stop discovery if a master node ACKs us
}

bool ESPNowTransport::_encryptPacket(uint8_t* plaintext, size_t* len) {
    if (!plaintext || !len) return false;
    
    uint8_t temp[256];
    if (!cryptoHAL.encrypt(plaintext, *len, _encryptionKey, temp)) {
        return false;
    }
    
    *len = *len + CRYPTO_OVERHEAD;
    memcpy(plaintext, temp, *len);
    return true;
}

bool ESPNowTransport::_decryptPacket(uint8_t* ciphertext, size_t* len) {
    if (!ciphertext || !len || *len <= CRYPTO_OVERHEAD) return false;
    
    uint8_t temp[256];
    if (!cryptoHAL.decrypt(ciphertext, *len, _encryptionKey, temp)) {
        return false;
    }
    
    *len = *len - CRYPTO_OVERHEAD;
    memcpy(ciphertext, temp, *len);
    return true;
}
