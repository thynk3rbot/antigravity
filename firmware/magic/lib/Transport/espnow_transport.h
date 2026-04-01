/**
 * @file espnow_transport.h
 * @brief ESP-NOW Transport Implementation
 */

#pragma once

#include "interface.h"
#include <esp_now.h>
#include <WiFi.h>
#include <vector>

/**
 * @struct EspNowPeer
 * @brief Tracking discovered peers
 */
struct EspNowPeer {
    uint8_t mac[6];
    bool discovered = false;
    uint32_t lastSeen = 0;
};

/**
 * @class ESPNowTransport
 * @brief Transport for fast node-to-node communication
 */
class ESPNowTransport : public TransportInterface {
public:
    static ESPNowTransport& getInstance();

    // TransportInterface implementation
    bool init() override;
    void shutdown() override;
    bool isReady() const override;
    int send(const uint8_t* payload, size_t len) override;
    int recv(uint8_t* buffer, size_t maxLen) override;
    bool isAvailable() const override;
    void poll() override;
    const char* getStatus() const override;
    int8_t getSignalStrength() const override;
    const char* getLastErrorString() const override;

    const char* getName() const override { return "ESP-NOW"; }
    TransportType getType() const override { return TransportType::ESPNOW; }

    // Discovery management
    void startDiscovery();
    void markDiscovered(const uint8_t* mac);

private:
    ESPNowTransport() = default;
    
    bool _initialized = false;
    int _lastError = 0;
    bool _discoveryActive = false;
    uint32_t _lastDiscoveryMs = 0;
    
    std::vector<EspNowPeer> _peers;
    
    // Static callbacks for esp_now
    static void _onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void _onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len);

    bool _encryptPacket(uint8_t* plaintext, size_t* len);
    bool _decryptPacket(uint8_t* ciphertext, size_t* len);
    
    uint8_t _encryptionKey[16];
};

extern ESPNowTransport& espNowTransport;
