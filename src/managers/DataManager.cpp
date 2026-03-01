#include "DataManager.h"
#include "../config.h"
#include "../crypto.h"
#include "../utils/DebugMacros.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>

DataManager::DataManager() {
  numNodes = 0;
  logIndex = 0;
  repeaterEnabled = false;
  encryptionActive = true;
  espNowEnabled = false;
  espNowChannel = ESPNOW_CHANNEL;
  numEspNowPeers = 0;
  myPrefix = "GW";
  myId = "GW-INIT";
  streamToSerial = false;
  mqttEnabled = false;
  mqttPort = 1883;
}

String DataManager::getHardwareSuffix() {
  uint64_t chipId = ESP.getEfuseMac();
  char suffix[12];
  sprintf(suffix, "%04X", (uint16_t)(chipId >> 32));
  return String(suffix);
}

String DataManager::getMacSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[12];
  sprintf(buf, "%06X", (uint32_t)((mac >> 24) & 0xFFFFFF));
  return String(buf);
}

void DataManager::Init() {
  Serial.println("INIT: DataManager Starting...");
  Serial.flush();
  InitFilesystem();

  Preferences p;
  p.begin("loralink", false);

  bootCount = p.getInt("bootCount", 0) + 1;
  p.putInt("bootCount", bootCount);
  Serial.printf("BOOT: #%d\n", bootCount);
  Serial.flush();

  // Initialize WiFi defaults if empty (first boot)
  String savedSsid = p.getString("wifi_ssid", "");
  if (savedSsid.length() == 0) {
    Serial.println("INIT: No WiFi credentials found - using defaults...");
    p.putString("wifi_ssid", "");
    p.putString("wifi_pass", "");
  }

  Serial.println("INIT: Loading Settings...");
  Serial.flush();
  p.end();
  LoadSettings();

  Serial.printf("BOOT: Heap Free: %u bytes\n", ESP.getFreeHeap());
  Serial.println("INIT: DataManager OK");
  Serial.flush();
}

bool DataManager::InitFilesystem() {
  if (!LittleFS.begin(true)) {
    LOG_PRINTLN("FS: LittleFS Mount Failed");
    return false;
  }
  LOG_PRINTLN("FS: LittleFS Mounted OK");
  return true;
}

bool DataManager::SaveSchedule(const String &json) {
  File file = LittleFS.open("/schedule.json", "w");
  if (!file) {
    LOG_PRINTLN("FS: Failed to open schedule for writing");
    return false;
  }
  size_t bytes = file.print(json);
  file.close();
  return (bytes > 0);
}

String DataManager::ReadSchedule() {
  if (!LittleFS.exists("/schedule.json"))
    return "{\"schedules\":[]}";
  File file = LittleFS.open("/schedule.json", "r");
  if (!file)
    return "{\"schedules\":[]}";
  String content = file.readString();
  file.close();
  return content;
}

void DataManager::LoadSettings() {
  Preferences p;
  p.begin("loralink", true);

  LOG_PRINTLN("INIT: Loading ID...");
  myId = p.getString("dev_name", myPrefix + "-" + getHardwareSuffix());
  if (myId.length() > 20)
    myId = myId.substring(0, 20);
  for (unsigned int i = 0; i < myId.length(); i++) {
    if (myId[i] < 0x20 || myId[i] > 0x7E)
      myId[i] = '?';
  }

  LOG_PRINTLN("INIT: Loading Repeater...");
  repeaterEnabled = p.getBool("repeater", false);

  LOG_PRINTLN("INIT: Loading WiFi...");
  wifiSsid = p.getString("wifi_ssid", "");
  wifiPass = p.getString("wifi_pass", "");

  LOG_PRINTLN("INIT: Loading IP...");
  staticIp = p.getString("static_ip", "");
  gateway = p.getString("gateway", "");
  subnet = p.getString("subnet", "");

  LOG_PRINTLN("INIT: Loading Crypto Key...");
  cryptoKey = p.getString("crypto_key", "");

  LOG_PRINTLN("INIT: Loading Sched...");
  schedulerInterval110V = p.getULong("sched_int_110", 5000);

  LOG_PRINTLN("INIT: Loading ESP-NOW...");
  espNowEnabled = p.getBool("espnow_en", false);
  espNowChannel = p.getUChar("espnow_ch", ESPNOW_CHANNEL);

  LOG_PRINTLN("INIT: Loading Radio Profiles...");
  wifiEnabled = p.getBool("wifi_en", true);
  bleEnabled = p.getBool("ble_en", true);

  LOG_PRINTLN("INIT: Loading Integrations...");
  mqttEnabled = p.getBool("mqtt_en", false);
  mqttServer = p.getString("mqtt_srv", "");
  mqttPort = p.getInt("mqtt_prt", 1883);
  mqttUser = p.getString("mqtt_usr", "");
  mqttPass = p.getString("mqtt_pwd", "");

  p.end();

  LoadESPNowPeers();
}

void DataManager::SaveSettings() {
  // Individual setters persist immediately
}

void DataManager::SetWifi(const String &ssid, const String &pass) {
  wifiSsid = ssid;
  wifiPass = pass;
  Preferences p;
  p.begin("loralink", false);
  p.putString("wifi_ssid", ssid);
  p.putString("wifi_pass", pass);
  p.end();
}

void DataManager::SetStaticIp(const String &ip, const String &gw,
                              const String &sn) {
  this->staticIp = ip;
  this->gateway = gw;
  this->subnet = sn;
  Preferences p;
  p.begin("loralink", false);
  p.putString("static_ip", ip);
  p.putString("gateway", gw);
  p.putString("subnet", sn);
  p.end();
}

void DataManager::SetName(const String &name) {
  myId = name;
  Preferences p;
  p.begin("loralink", false);
  p.putString("dev_name", name);
  p.end();
}

void DataManager::SetRepeater(bool enabled) {
  repeaterEnabled = enabled;
  Preferences p;
  p.begin("loralink", false);
  p.putBool("repeater", enabled);
  p.end();
}

void DataManager::SetCryptoKey(const String &hexKey) {
  cryptoKey = hexKey;
  Preferences p;
  p.begin("loralink", false);
  p.putString("crypto_key", hexKey);
  p.end();
}

bool DataManager::GetCryptoKey(uint8_t *keyBuf) {
  if (cryptoKey.length() == 32) {
    return parseHexKey(cryptoKey.c_str(), keyBuf);
  }
  return false;
}

void DataManager::SetMqtt(bool enabled, const String &server, int port,
                          const String &user, const String &pass) {
  this->mqttEnabled = enabled;
  this->mqttServer = server;
  this->mqttPort = port;
  this->mqttUser = user;
  this->mqttPass = pass;

  Preferences p;
  p.begin("loralink", false);
  p.putBool("mqtt_en", enabled);
  p.putString("mqtt_srv", server);
  p.putInt("mqtt_prt", port);
  p.putString("mqtt_usr", user);
  p.putString("mqtt_pwd", pass);
  p.end();
}

void DataManager::SetESPNowEnabled(bool enabled) {
  espNowEnabled = enabled;
  Preferences p;
  p.begin("loralink", false);
  p.putBool("espnow_en", enabled);
  p.end();
}

void DataManager::SetWifiEnabled(bool enabled) {
  wifiEnabled = enabled;
  Preferences p;
  p.begin("loralink", false);
  p.putBool("wifi_en", enabled);
  p.end();
}

void DataManager::SetBleEnabled(bool enabled) {
  bleEnabled = enabled;
  Preferences p;
  p.begin("loralink", false);
  p.putBool("ble_en", enabled);
  p.end();
}

void DataManager::SaveESPNowPeer(int index, const uint8_t *mac,
                                 const char *name) {
  if (index < 0 || index >= ESPNOW_MAX_PEERS)
    return;

  memcpy(espNowPeers[index].mac, mac, 6);
  strncpy(espNowPeers[index].name, name, 15);
  espNowPeers[index].name[15] = '\0';
  espNowPeers[index].active = true;

  Preferences p;
  p.begin("espnow", false);
  String key_mac = "peer_mac_" + String(index);
  String key_name = "peer_name_" + String(index);
  p.putBytes(key_mac.c_str(), mac, 6);
  p.putString(key_name.c_str(), name);
  p.putInt("peer_count", max(numEspNowPeers, index + 1));
  p.end();

  if (index >= numEspNowPeers)
    numEspNowPeers = index + 1;
}

void DataManager::RemoveESPNowPeer(int index) {
  if (index < 0 || index >= ESPNOW_MAX_PEERS)
    return;
  espNowPeers[index].active = false;
  memset(espNowPeers[index].mac, 0, 6);
  espNowPeers[index].name[0] = '\0';
}

void DataManager::LoadESPNowPeers() {
  Preferences p;
  p.begin("espnow", true);
  numEspNowPeers = p.getInt("peer_count", 0);
  if (numEspNowPeers > ESPNOW_MAX_PEERS)
    numEspNowPeers = ESPNOW_MAX_PEERS;

  for (int i = 0; i < numEspNowPeers; i++) {
    String key_mac = "peer_mac_" + String(i);
    String key_name = "peer_name_" + String(i);
    size_t len = p.getBytes(key_mac.c_str(), espNowPeers[i].mac, 6);
    String name = p.getString(key_name.c_str(), "");
    strncpy(espNowPeers[i].name, name.c_str(), 15);
    espNowPeers[i].name[15] = '\0';
    espNowPeers[i].active = (len == 6);
  }
  p.end();
  LOG_PRINTF("INIT: Loaded %d ESP-NOW peers\n", numEspNowPeers);
}

void DataManager::UpdateNode(const char *id, uint32_t uptime, float battery,
                             uint8_t resetCode, float lat, float lon, int rssi,
                             uint8_t hops) {
  if (strcmp(id, myId.c_str()) == 0)
    return;
  for (int i = 0; i < numNodes; i++) {
    if (strcmp(remoteNodes[i].id, id) == 0) {
      remoteNodes[i].lastSeen = millis();
      remoteNodes[i].battery = battery;
      remoteNodes[i].resetCode = resetCode;
      remoteNodes[i].uptime = uptime;
      remoteNodes[i].rssi = rssi;
      remoteNodes[i].hops = hops;
      remoteNodes[i].lat = lat;
      remoteNodes[i].lon = lon;
      return;
    }
  }
  if (numNodes < MAX_NODES) {
    strncpy(remoteNodes[numNodes].id, id, 15);
    remoteNodes[numNodes].id[15] = 0;
    remoteNodes[numNodes].lastSeen = millis();
    remoteNodes[numNodes].battery = battery;
    remoteNodes[numNodes].resetCode = resetCode;
    remoteNodes[numNodes].uptime = uptime;
    remoteNodes[numNodes].rssi = rssi;
    remoteNodes[numNodes].hops = hops;
    remoteNodes[numNodes].lat = lat;
    remoteNodes[numNodes].lon = lon;
    numNodes++;
  }
}

void DataManager::SawNode(const char *id, int rssi, uint8_t hops) {
  if (strcmp(id, myId.c_str()) == 0)
    return;
  for (int i = 0; i < numNodes; i++) {
    if (strcmp(remoteNodes[i].id, id) == 0) {
      remoteNodes[i].lastSeen = millis();
      remoteNodes[i].rssi = rssi;
      remoteNodes[i].hops = hops;
      return;
    }
  }
  if (numNodes < MAX_NODES) {
    strncpy(remoteNodes[numNodes].id, id, 15);
    remoteNodes[numNodes].id[15] = 0;
    remoteNodes[numNodes].lastSeen = millis();
    remoteNodes[numNodes].battery = 0.0f;
    remoteNodes[numNodes].resetCode = 0;
    remoteNodes[numNodes].uptime = 0;
    remoteNodes[numNodes].rssi = rssi;
    remoteNodes[numNodes].hops = hops;
    remoteNodes[numNodes].lat = 0.0f;
    remoteNodes[numNodes].lon = 0.0f;
    numNodes++;
  }
}

void DataManager::PruneStaleNodes() {
  unsigned long now = millis();
  for (int i = 0; i < numNodes; i++) {
    if (now - remoteNodes[i].lastSeen > 300000) { // 5 minutes
      LOG_PRINTF("MESH: Pruned stale node: %s\n", remoteNodes[i].id);
      // Shift remaining nodes down
      for (int j = i; j < numNodes - 1; j++) {
        remoteNodes[j] = remoteNodes[j + 1];
      }
      numNodes--;
      i--; // Re-check this index
    }
  }
}

void DataManager::LogMessage(const String &source, int rssi,
                             const String &msg) {
  msgLog[logIndex].timestamp = millis() / 1000;
  msgLog[logIndex].source = source;
  msgLog[logIndex].rssi = rssi;
  msgLog[logIndex].message = msg;
  logIndex = (logIndex + 1) % LOG_SIZE;
}

void DataManager::SetSchedulerInterval(unsigned long ms) {
  schedulerInterval110V = ms;
  Preferences p;
  p.begin("loralink", false);
  p.putULong("sched_int_110", ms);
  p.end();
}

void DataManager::SetGpioState(const String &pinName, bool state) {
  Preferences p;
  p.begin("lora_hw", false);
  p.putBool(pinName.c_str(), state);
  p.end();
}

bool DataManager::GetGpioState(const String &pinName) {
  Preferences p;
  p.begin("lora_hw", true);
  bool state = p.getBool(pinName.c_str(), false);
  p.end();
  return state;
}

void DataManager::FactoryReset() {
  LOG_PRINTLN("SYS: FACTORY RESET (Clearing NVS)...");
  Preferences p;
  p.begin("loralink", false);
  p.clear();
  p.end();
  p.begin("lora_hw", false);
  p.clear();
  p.end();
  p.begin("espnow", false);
  p.clear();
  p.end();
}

String DataManager::getResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
  case ESP_RST_POWERON:
    return "Power On";
  case ESP_RST_EXT:
    return "Ext Pin";
  case ESP_RST_SW:
    return "Soft Reset";
  case ESP_RST_PANIC:
    return "Crash";
  case ESP_RST_INT_WDT:
    return "Int WDT";
  case ESP_RST_TASK_WDT:
    return "Task WDT";
  case ESP_RST_WDT:
    return "Other WDT";
  case ESP_RST_DEEPSLEEP:
    return "Deep Sleep";
  case ESP_RST_BROWNOUT:
    return "Brownout";
  default:
    return "Unknown";
  }
}
