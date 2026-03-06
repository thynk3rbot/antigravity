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

  // Returns true if a PC webapp has hit the API within the given window (default 5 min)
  bool isPCAttached(unsigned long windowMs = 300000UL) const {
    return isConnected && lastApiHit > 0 &&
           (millis() - lastApiHit) < windowMs;
  }

  // Returns true if running on USB/mains (battery reads near 0V or > 4.1V charging)
  static bool isPowered() {
    float bat = analogRead(PIN_BAT_ADC) / 4095.0f * 3.3f * 2.0f;
    return (bat < 0.1f || bat > 4.1f);
  }

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
  void serveHardware();
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
  void serveApiProductSave();
};

#endif // WIFI_MANAGER_H
