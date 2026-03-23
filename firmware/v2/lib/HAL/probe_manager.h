#ifndef LORALINK_HAL_PROBE_MANAGER_H
#define LORALINK_HAL_PROBE_MANAGER_H

#include <Arduino.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @struct DetectedDevice
 * @brief Represents a device discovered via passive Wi-Fi or BLE sniffing.
 */
struct DetectedDevice {
  uint8_t mac[6];
  int8_t rssi;
  uint32_t lastSeen;
  std::string ssid; // For Wi-Fi APs or BLE names
  bool isSTA;       // True if station (probe req), false if AP (beacon)
  bool isBLE;       // True if discovered via BLE scanning
  
  std::string getMacStr() const {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
  }

  bool operator<(const DetectedDevice& other) const {
    if (isBLE != other.isBLE) return isBLE < other.isBLE;
    for (int i = 0; i < 6; i++) {
        if (mac[i] < other.mac[i]) return true;
        if (mac[i] > other.mac[i]) return false;
    }
    return false;
  }
};

/**
 * @class ProbeManager
 * @brief Singleton manager for passive "Industrial" sniffing (Marauder Integration).
 * 
 * Handles background Wi-Fi promiscuous mode and BLE scanning to enable
 * presence detection and asset tracking.
 */
class ProbeManager {
public:
  static ProbeManager& getInstance() {
    static ProbeManager instance;
    return instance;
  }

  bool init();
  void service();

  // Sniffer Control
  void setSniffing(bool enabled);
  bool isSniffing() const { return _sniffing; }
  
  // Channel Hopping Logic
  void setAutoHopping(bool enabled);
  void nextChannel();

  // Registry Access
  const std::vector<DetectedDevice>& getDetectedDevices() const;
  void clearRegistry();

private:
  ProbeManager() : _sniffing(false), _autoHop(false), _lastHopTime(0), _currentChannel(1) {}
  ~ProbeManager() {}

  // BLE Scanning Support
  class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  public:
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override;
  };
  ScanCallbacks _bleCallbacks;
  NimBLEScan* _bleScan = nullptr;

  // Forbidden patterns
  ProbeManager(const ProbeManager&) = delete;
  ProbeManager& operator=(const ProbeManager&) = delete;

  // Internal Callbacks
  static void wifiSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type);
  void processPacket(void* buf, wifi_promiscuous_pkt_type_t type);
  void updateRegistry(const DetectedDevice& dev);

  bool _sniffing;
  bool _autoHop;
  uint32_t _lastHopTime;
  uint8_t _currentChannel;
  
  std::vector<DetectedDevice> _devices;
  
  static const uint32_t HOP_INTERVAL_MS = 300;
  static const size_t MAX_DEVICES = 50;
};

#endif // LORALINK_HAL_PROBE_MANAGER_H
