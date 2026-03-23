#include "probe_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Initialize static members if any (none currently besides singleton)

bool ProbeManager::init() {
    // Wi-Fi must be in STA mode for promiscuous mode to work correctly on ESP32
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // We don't start sniffing by default to save power/cpu until requested
    return true;
}

void ProbeManager::setSniffing(bool enabled) {
    if (_sniffing == enabled) return;
    _sniffing = enabled;
    
    if (_sniffing) {
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&ProbeManager::wifiSnifferCallback);
    } else {
        esp_wifi_set_promiscuous(false);
    }
}

void ProbeManager::setAutoHopping(bool enabled) {
    _autoHop = enabled;
}

void ProbeManager::nextChannel() {
    _currentChannel++;
    if (_currentChannel > 13) _currentChannel = 1;
    esp_wifi_set_channel(_currentChannel, WIFI_SECOND_CHAN_NONE);
}

void ProbeManager::service() {
    if (!_sniffing) return;
    
    if (_autoHop) {
        uint32_t now = millis();
        if (now - _lastHopTime >= HOP_INTERVAL_MS) {
            nextChannel();
            _lastHopTime = now;
        }
    }
}

void ProbeManager::clearRegistry() {
    _devices.clear();
}

const std::vector<DetectedDevice>& ProbeManager::getDetectedDevices() const {
    return _devices;
}

void ProbeManager::wifiSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    ProbeManager::getInstance().processPacket(buf, type);
}

void ProbeManager::processPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    
    // Management frame check (Probe Request = 0x40, Beacon = 0x80)
    uint8_t frameControl = payload[0];
    bool isProbe = (frameControl == 0x40);
    bool isBeacon = (frameControl == 0x80);
    
    if (!isProbe && !isBeacon) return;
    
    DetectedDevice dev;
    // Source MAC is at offset 10
    memcpy(dev.mac, &payload[10], 6);
    dev.rssi = pkt->rx_ctrl.rssi;
    dev.lastSeen = millis();
    dev.isSTA = isProbe;
    
    if (isBeacon) {
        // Tagged params start at offset 36
        // SSID is Tag 0
        uint8_t ssidLen = payload[37];
        if (ssidLen > 0 && ssidLen <= 32) {
            char ssidBuf[33];
            memcpy(ssidBuf, &payload[38], ssidLen);
            ssidBuf[ssidLen] = '\0';
            dev.ssid = std::string(ssidBuf);
        }
    }
    
    // Add or update in registry
    bool found = false;
    for (auto& existing : _devices) {
        if (memcmp(existing.mac, dev.mac, 6) == 0) {
            existing.rssi = dev.rssi;
            existing.lastSeen = dev.lastSeen;
            if (!dev.ssid.empty()) existing.ssid = dev.ssid;
            found = true;
            break;
        }
    }
    
    if (!found) {
        if (_devices.size() >= MAX_DEVICES) {
            _devices.erase(_devices.begin());
        }
        _devices.push_back(dev);
    }
}
