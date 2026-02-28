#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

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

  // ESP-NOW Settings
  bool espNowEnabled;
  uint8_t espNowChannel;

  // State
  int bootCount;
  RemoteNode remoteNodes[MAX_NODES];
  int numNodes;
  String msgLog[LOG_SIZE];
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
  void SetAesKey(uint8_t *key);
  bool GetAesKey(uint8_t *keyBuf);

  // ESP-NOW Persistence
  void SetESPNowEnabled(bool enabled);
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
  void UpdateNode(const char *id, TelemetryPacket *tp, int rssi);
  void LogMessage(const String &msg);

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
