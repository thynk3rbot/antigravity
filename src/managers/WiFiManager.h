#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "../config.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>

void setWebCallback(void (*cb)(const String &, CommInterface));

class WiFiManager {
public:
  static WiFiManager &getInstance() {
    static WiFiManager instance;
    return instance;
  }

  void init();
  void handle();
  bool isConnected;
  unsigned long lastApiHit;
  bool modemSleepEnabled;

private:
  WiFiManager();
  bool serverStarted;
  unsigned long lastWifiTry;
  void tryConnect();
  void startServer();

  // Page handlers
  void serveHome();
  void serveConfig();
  void serveConfigSave();
  void serveIntegration();
  void serveIntegrationSave();
  void serveHelp();
  void serveScheduling();
  void serveApiStatus();
  void serveApiConfig();
  void serveApiConfigApply();
  void serveApiFileList();
  void serveApiFileRead();
  void serveApiCmd();
  void serveApiPeers();
  void serveApiAddPeer();
  void serveApiRemovePeer();
  void serveApiSchedule();
  void serveApiScheduleAdd();
  void serveApiScheduleRemove();
  void serveApiScheduleClear();
  void serveApiScheduleSave();
  void serveApiPinName();
  void serveApiPinEnable();
  void serveApiTransportMode();
  void serveApiRegistry();
};

#endif // WIFI_MANAGER_H
