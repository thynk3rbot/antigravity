#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <Preferences.h>

struct LogEntry {
  uint32_t timestamp;
  String source;
  int rssi;
  String message;
};

class DataManager {
public:
  static DataManager &getInstance() {
    static DataManager instance;
    return instance;
  }

  // Settings
  String myId;
  String myPrefix;
  bool repeaterEnabled;
  bool encryptionActive;
  String wifiSsid;
  String wifiPass;
  String staticIp;
  String gateway;
  String subnet;
  String cryptoKey;

  // Integrations
  bool streamToSerial;
  bool mqttEnabled;
  String mqttServer;
  int mqttPort;
  String mqttUser;
  String mqttPass;

  // ESP-NOW & Protocol Settings
  bool espNowEnabled;
  uint8_t espNowChannel;
  bool wifiEnabled;
  bool bleEnabled;

  // State
  int bootCount;
  RemoteNode remoteNodes[MAX_NODES];
  int numNodes;
  LogEntry msgLog[LOG_SIZE];
  int logIndex;

  // ESP-NOW Peers
  ESPNowPeer espNowPeers[ESPNOW_MAX_PEERS];
  int numEspNowPeers;

  // Config Methods
  void Init();
  void LoadSettings();
  void SaveSettings();
  void SetWifi(const String &ssid, const String &pass);
  void SetStaticIp(const String &ip, const String &gateway,
                   const String &subnet);
  void SetName(const String &name);
  void SetRepeater(bool enabled);
  void SetCryptoKey(const String &hexKey);
  bool GetCryptoKey(uint8_t *keyBuf);
  void SetMqtt(bool enabled, const String &server, int port, const String &user,
               const String &pass);

  // Persistence
  void SetESPNowEnabled(bool enabled);
  void SetWifiEnabled(bool enabled);
  void SetBleEnabled(bool enabled);
  void SaveESPNowPeer(int index, const uint8_t *mac, const char *name);
  void RemoveESPNowPeer(int index);
  void LoadESPNowPeers();

  // Scheduler
  unsigned long schedulerInterval110V;
  void SetSchedulerInterval(unsigned long ms);

  // Persistence: GPIO States
  void SetGpioState(const String &pinName, bool state);
  bool GetGpioState(const String &pinName);
  void FactoryReset();

  // Node & Log Methods
  void UpdateNode(const char *id, uint32_t uptime, float battery,
                  uint8_t resetCode, float lat, float lon, int rssi,
                  uint8_t hops = 0);
  void SawNode(const char *id, int rssi, uint8_t hops);
  void PruneStaleNodes();
  void LogMessage(const String &source, int rssi, const String &msg);

  // Filesystem Search/Load
  bool SaveSchedule(const String &json);
  String ReadSchedule();
  bool InitFilesystem();

  // Utils
  String getHardwareSuffix();
  String getMacSuffix();
  String getResetReason();

private:
  DataManager();
};

#endif // DATA_MANAGER_H
