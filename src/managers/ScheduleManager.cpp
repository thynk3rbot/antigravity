#include "ScheduleManager.h"
#include "../utils/DebugMacros.h"
#include "BLEManager.h"
#include "CommandManager.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "ESPNowManager.h"
#include "LoRaManager.h"
#include "WiFiManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskScheduler.h>

static void showStartupText(const char *msg) {
  if (Heltec.display) {
    Heltec.display->clear();
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
    Heltec.display->drawString(64, 20, "BOOT SEQUENCE");
    Heltec.display->setFont(ArialMT_Plain_16);
    Heltec.display->drawString(64, 40, msg);
    Heltec.display->display();
  }
}

ScheduleManager *instance_ptr = nullptr;

ScheduleManager::ScheduleManager()
    : dht(PIN_SENSOR_DHT, DHT22), dhtFailCount(0), safetyTripped(false),
      tEnvironmental(10000, TASK_FOREVER,
                     &ScheduleManager::environmentalCallback),
      t110V(5000, TASK_FOREVER, &ScheduleManager::toggle110VCallback),
      t12V(3600000, TASK_FOREVER, &ScheduleManager::pulse12VCallback),
      t12VEnd(5000, TASK_ONCE, &ScheduleManager::endPulse12VCallback),
      tLoRa(500, TASK_FOREVER, &ScheduleManager::loraTask),
      tWiFi(1000, TASK_FOREVER, &ScheduleManager::wifiTask),
      tSerial(100, TASK_FOREVER, &ScheduleManager::serialTask),
      tHeartbeat(60000, TASK_FOREVER, &ScheduleManager::heartbeatTask),
      tButton(50, TASK_FOREVER, &ScheduleManager::buttonTask),
      tDisplay(2000, TASK_FOREVER, &ScheduleManager::displayTask),
      tBlink(200, 20, &ScheduleManager::blinkTask),
      tRestart(1000, TASK_ONCE, &ScheduleManager::restartTask),
      tBLE(20, TASK_FOREVER, &ScheduleManager::bleTask),
      tESPNow(50, TASK_FOREVER, &ScheduleManager::espNowTask),
      tBatteryMonitor(300000, TASK_FOREVER,
                      &ScheduleManager::batteryMonitorCallback),
      blinkCount(0) {
  instance_ptr = this;
}

void ScheduleManager::init() {
  LOG_PRINTLN("SCHED: Initializing Tasks");

  pinMode(PIN_RELAY_110V, OUTPUT);
  pinMode(PIN_RELAY_12V_1, OUTPUT);
  pinMode(PIN_RELAY_12V_2, OUTPUT);
  pinMode(PIN_RELAY_12V_3, OUTPUT);

  digitalWrite(PIN_RELAY_110V, LOW);
  digitalWrite(PIN_RELAY_12V_1, LOW);
  digitalWrite(PIN_RELAY_12V_2, LOW);
  digitalWrite(PIN_RELAY_12V_3, LOW);

  pinMode(PIN_BUTTON_PRG, INPUT_PULLUP);

  runner.init();
  runner.addTask(tEnvironmental);
  runner.addTask(t110V);
  runner.addTask(t12V);
  runner.addTask(t12VEnd);
  runner.addTask(tLoRa);
  runner.addTask(tWiFi);
  runner.addTask(tSerial);
  runner.addTask(tHeartbeat);
  runner.addTask(tButton);
  runner.addTask(tDisplay);
  runner.addTask(tBlink);
  runner.addTask(tRestart);
  runner.addTask(tBLE);
  runner.addTask(tESPNow);
  runner.addTask(tBatteryMonitor);

  // Load Saved Interval
  unsigned long savedInterval =
      DataManager::getInstance().schedulerInterval110V;
  if (savedInterval > 0) {
    t110V.setInterval(savedInterval);
    LOG_PRINTF("SCHED: Loaded 110V Interval: %lu ms\n", savedInterval);
  }

  tDisplay.disable();
  showStartupText("System Booting...");

  tEnvironmental.enableDelayed(5000);
  t110V.enableDelayed();
  t12V.enableDelayed();
  tSerial.enable();
  tButton.enable();
  tDisplay.enable();
  if (DataManager::getInstance().bleEnabled) {
    tBLE.enable();
  }
  tLoRa.enableDelayed(2000);
  if (DataManager::getInstance().wifiEnabled) {
    tWiFi.enableDelayed(3000);
  }
  // Random jitter to desynchronize heartbeats across nodes
  unsigned long hbJitter = 8000 + random(0, 17000);
  tHeartbeat.enableDelayed(hbJitter);
  LOG_PRINTF("SCHED: Heartbeat start jitter: %lu ms\n", hbJitter);

  // ESP-NOW task
  if (DataManager::getInstance().espNowEnabled) {
    tESPNow.enable();
  }

  // Battery protection monitor
  tBatteryMonitor.enableDelayed(10000);

  // Dynamic Tasks
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    tDynamicPool[i].set(0, TASK_FOREVER, &ScheduleManager::dynamicTaskCallback);
    runner.addTask(tDynamicPool[i]);
  }
  loadDynamicSchedules();
}

void ScheduleManager::execute() { runner.execute(); }

// --- Task Callbacks ---

void ScheduleManager::environmentalCallback() {
  static bool initial = true;
  if (initial) {
    showStartupText("Sensors... OK");
    initial = false;
  }
  return; // DISABLED (No sensors installed)
}

void ScheduleManager::toggle110VCallback() {
  static bool state = false;
  state = !state;
  digitalWrite(PIN_RELAY_110V, state ? HIGH : LOW);
  LOG_PRINTF("SCHED: 110V Toggle -> %s\n", state ? "ON" : "OFF");
}

void ScheduleManager::pulse12VCallback() {
  LOG_PRINTLN("SCHED: 12V Pulse Start (5s)");
  digitalWrite(PIN_RELAY_12V_1, HIGH);
  instance_ptr->t12VEnd.restartDelayed(5000);
}

void ScheduleManager::endPulse12VCallback() {
  LOG_PRINTLN("SCHED: 12V Pulse End");
  digitalWrite(PIN_RELAY_12V_1, LOW);
}

void ScheduleManager::loraTask() {
  static bool initial = true;
  if (initial) {
    showStartupText("LoRa... OK");
    initial = false;
  }
  LoRaManager::getInstance().HandleRx();
}

void ScheduleManager::wifiTask() { WiFiManager::getInstance().handle(); }

void ScheduleManager::serialTask() {
  ScheduleManager &inst = getInstance();
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      if (inst.isStreaming) {
        inst.processStreamLine(input, CommInterface::COMM_SERIAL);
      } else {
        CommandManager::getInstance().handleCommand(input,
                                                    CommInterface::COMM_SERIAL);
      }
    }
  }
}

void ScheduleManager::heartbeatTask() {
  static bool initial = true;
  if (initial) {
    showStartupText("Heartbeat... OK");
    initial = false;
  }
  LoRaManager::getInstance().SendHeartbeat();
  DataManager::getInstance().PruneStaleNodes();
}

void ScheduleManager::bleTask() {
  static bool initial = true;
  if (initial) {
    BLEManager::getInstance().init();
    if (instance_ptr)
      instance_ptr->tDisplay.enable();
    initial = false;
  }
  String cmd;
  while (BLEManager::getInstance().poll(cmd)) {
    DataManager::getInstance().LogMessage("BLE", 0, cmd);
    CommandManager::getInstance().handleCommand(cmd, CommInterface::COMM_BLE);
  }
}

void ScheduleManager::espNowTask() {
  String msg;
  while (ESPNowManager::getInstance().poll(msg)) {
    DataManager::getInstance().LogMessage("ESPNOW", 0, msg);
    CommandManager::getInstance().handleCommand(msg,
                                                CommInterface::COMM_ESPNOW);
  }
}

void ScheduleManager::buttonTask() {
  static unsigned long lastBtn = 0;
  static bool lastState = HIGH;
  bool currentState = digitalRead(PIN_BUTTON_PRG);

  if (currentState == LOW && lastState == HIGH) {
    if (millis() - lastBtn > 1000) {
      lastBtn = millis();
      LOG_PRINTLN("BTN: PRG Pressed - Paging...");
      DisplayManager::getInstance().NextPage();
    }
  }
  lastState = currentState;
}

void ScheduleManager::displayTask() {
  DisplayManager &disp = DisplayManager::getInstance();
  if (disp.IsDisplayActive()) {
    if (millis() - disp.lastDisplayActivity > 30000) {
      disp.SetDisplayActive(false);
    } else {
      disp.DrawUi();
    }
  }
}

void ScheduleManager::blinkTask() {
  if (!instance_ptr)
    return;
  bool ledOn = (instance_ptr->blinkCount % 2 == 0);
  digitalWrite(PIN_LED_BUILTIN, ledOn ? HIGH : LOW);
  instance_ptr->blinkCount++;
  if (instance_ptr->blinkCount >= 20) {
    digitalWrite(PIN_LED_BUILTIN, LOW);
    instance_ptr->tBlink.disable();
    LOG_PRINTLN("SCHED: Blink Done");
  }
}

void ScheduleManager::restartTask() {
  LOG_PRINTLN("SCHED: Restarting...");
  ESP.restart();
}

// --- Management Methods ---

void ScheduleManager::checkSafetyThreshold(float temp) {
  if (temp > 45.0) {
    LOG_PRINTLN("SCHED: OVERHEAT - Emergency Stop 110V");
    t110V.disable();
    digitalWrite(PIN_RELAY_110V, LOW);
    safetyTripped = true;
  }
}

void ScheduleManager::set110VInterval(unsigned long interval_ms) {
  t110V.setInterval(interval_ms);
  LOG_PRINTF("SCHED: 110V Interval set to %lu ms\n", interval_ms);
}

void ScheduleManager::forceRelay110V(bool state) {
  t110V.disable();
  digitalWrite(PIN_RELAY_110V, state ? HIGH : LOW);
  LOG_PRINTF("SCHED: 110V Manual Force -> %s\n", state ? "ON" : "OFF");
}

void ScheduleManager::trigger12VPulse() {
  t12V.forceNextIteration();
  LOG_PRINTLN("SCHED: 12V Pulse Manual Trigger");
}

void ScheduleManager::triggerBlink() {
  if (!instance_ptr)
    return;
  instance_ptr->blinkCount = 0;
  instance_ptr->tBlink.restart();
  LOG_PRINTLN("SCHED: Blink Started");
}

void ScheduleManager::triggerRestart(unsigned long delayMs) {
  if (!instance_ptr)
    return;
  instance_ptr->tRestart.setInterval(delayMs);
  instance_ptr->tRestart.restart();
  LOG_PRINTLN("SCHED: Restart Scheduled");
}

void ScheduleManager::loadDynamicSchedules() {
  DataManager &data = DataManager::getInstance();
  String json = data.ReadSchedule();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    LOG_PRINTLN("SCHED: JSON Parse Fail");
    return;
  }

  JsonArray schedules = doc["schedules"].as<JsonArray>();
  activeDynamicTasks = 0;

  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    tDynamicPool[i].disable();
  }

  int i = 0;
  for (JsonObject s : schedules) {
    if (i >= MAX_DYNAMIC_TASKS)
      break;
    dynamicConfigs[i].name = s["name"] | "Task";
    dynamicConfigs[i].type = s["type"] | "TOGGLE";
    dynamicConfigs[i].pin =
        CommandManager::getInstance().getPinFromName(s["pin"] | "");
    dynamicConfigs[i].interval = s["interval"] | 0;
    dynamicConfigs[i].duration = s["duration"] | 0;
    dynamicConfigs[i].enabled = s["enabled"] | true;
    dynamicConfigs[i].updatedBy = s["updatedBy"] | "UNKNOWN";
    dynamicConfigs[i].lastUpdated = s["lastUpdated"] | "---";

    if (dynamicConfigs[i].pin >= 0 && dynamicConfigs[i].interval > 0) {
      tDynamicPool[i].setInterval(dynamicConfigs[i].interval);
      if (dynamicConfigs[i].enabled)
        tDynamicPool[i].enable();
      activeDynamicTasks++;
    }
    i++;
  }
}

void ScheduleManager::dynamicTaskCallback() {
  if (!instance_ptr)
    return;
  Task &t = instance_ptr->runner.currentTask();

  int idx = -1;
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (&t == &(instance_ptr->tDynamicPool[i])) {
      idx = i;
      break;
    }
  }

  if (idx >= 0) {
    DynamicTaskConfig &cfg = instance_ptr->dynamicConfigs[idx];
    if (cfg.type == "TOGGLE") {
      bool cur = digitalRead(cfg.pin);
      digitalWrite(cfg.pin, !cur);
      LOG_PRINTF("SCHED: %s Toggle -> %d\n", cfg.name.c_str(), !cur);
    } else if (cfg.type == "PULSE") {
      static unsigned long pulseStart[MAX_DYNAMIC_TASKS] = {0};
      static bool pulseActive[MAX_DYNAMIC_TASKS] = {false};

      if (!pulseActive[idx]) {
        digitalWrite(cfg.pin, HIGH);
        pulseStart[idx] = millis();
        pulseActive[idx] = true;
      } else if (millis() - pulseStart[idx] >=
                 (cfg.duration > 0 ? cfg.duration : 5000)) {
        digitalWrite(cfg.pin, LOW);
        pulseActive[idx] = false;
      }
    }
  }
}

void ScheduleManager::loadSchedulesFromCsv(const String &csv) {
  LOG_PRINTLN("SCHED: Parsing CSV schedule...");

  JsonDocument doc;
  JsonArray schedules = doc["schedules"].to<JsonArray>();

  int start = 0;
  int end = csv.indexOf(';');
  while (start < (int)csv.length()) {
    if (end == -1)
      end = csv.length();
    String line = csv.substring(start, end);

    String parts[6];
    int pIdx = 0;
    int pStart = 0;
    int pEnd = line.indexOf(',');
    while (pIdx < 6) {
      if (pEnd == -1)
        pEnd = line.length();
      parts[pIdx++] = line.substring(pStart, pEnd);
      if (pEnd == (int)line.length())
        break;
      pStart = pEnd + 1;
      pEnd = line.indexOf(',', pStart);
    }

    if (pIdx >= 4) {
      JsonObject obj = schedules.add<JsonObject>();
      obj["name"] = parts[0];
      obj["type"] = parts[1];
      obj["pin"] = parts[2];
      obj["interval"] = parts[3].toInt();
      obj["duration"] = (pIdx > 4) ? parts[4].toInt() : 0;
      obj["enabled"] = (pIdx > 5) ? (parts[5].toInt() == 1) : true;
    }

    start = end + 1;
    end = csv.indexOf(';', start);
  }

  String json;
  serializeJson(doc, json);
  DataManager::getInstance().SaveSchedule(json);
  loadDynamicSchedules();
}

void ScheduleManager::batteryMonitorCallback() {
  float bat = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
  LOG_PRINTF("SCHED: Battery Monitor -> %.2fV\n", bat);
  if (bat > 0.5 && bat < 3.20) {
    LOG_PRINTLN("SCHED: Low Battery! Entering Deep Sleep.");
    DataManager::getInstance().LogMessage("SYS", 0, "Low Battery Sleep");
    if (LoRaManager::getInstance().loraActive) {
      LoRaManager::getInstance().SendLoRa(DataManager::getInstance().myId +
                                          " SYS: Low Battery Sleep");
    }
    delay(1000);
    if (Heltec.display) {
      Heltec.display->clear();
      Heltec.display->drawString(0, 0, "LOW BATTERY SLEEP");
      Heltec.display->display();
      delay(2000);
      Heltec.display->displayOff();
    }
    esp_sleep_enable_timer_wakeup(6ULL * 60 * 60 * 1000000ULL); // 6 Hours
    esp_deep_sleep_start();
  }
}

void ScheduleManager::getTaskJson(JsonDocument &doc) {
  JsonArray schedules = doc["schedules"].to<JsonArray>();
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (tDynamicPool[i].isEnabled() || dynamicConfigs[i].name.length() > 0) {
      JsonObject obj = schedules.add<JsonObject>();
      obj["name"] = dynamicConfigs[i].name;
      obj["type"] = dynamicConfigs[i].type;
      obj["pin"] = String(dynamicConfigs[i].pin);
      String friendly =
          DataManager::getInstance().GetPinName(String(dynamicConfigs[i].pin));
      if (friendly.length() > 0) {
        obj["pinName"] = friendly;
      }
      obj["interval"] = dynamicConfigs[i].interval / 1000;
      obj["duration"] = dynamicConfigs[i].duration / 1000;
      obj["enabled"] = dynamicConfigs[i].enabled;

      // Live Status
      obj["state"] = digitalRead(dynamicConfigs[i].pin);
      obj["nextRun"] = runner.timeUntilNextIteration(tDynamicPool[i]) / 1000;

      obj["updatedBy"] = dynamicConfigs[i].updatedBy;
      obj["lastUpdated"] = dynamicConfigs[i].lastUpdated;
    }
  }
}

String ScheduleManager::getTaskReport() {
  String out = "--- SYSTEM TASKS ---\n";
  Task *tasks[] = {&tLoRa, &tWiFi,   &tSerial, &tHeartbeat,
                   &tBLE,  &tESPNow, &tButton, &tDisplay,
                   &t110V, &t12V,    &tBlink,  &tBatteryMonitor};
  const char *names[] = {"LoRa", "WiFi",      "Serial", "HEARTBEAT",
                         "BLE",  "ESP-NOW",   "Button", "Display",
                         "110V", "12V_Pulse", "Blink",  "Battery"};

  for (int i = 0; i < 12; i++) {
    out += String(names[i]) + ": " + (tasks[i]->isEnabled() ? "ON" : "OFF");
    out += " (" + String(tasks[i]->getInterval() / 1000.0, 1) + "s)\n";
  }

  out += "\n--- DYNAMIC SCHEDULES (" + String(activeDynamicTasks) + "/" +
         String(MAX_DYNAMIC_TASKS) + ") ---\n";
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (tDynamicPool[i].isEnabled()) {
      out += "[" + String(i) + "] " + dynamicConfigs[i].name + " (" +
             dynamicConfigs[i].type + ") ";
      String friendly =
          DataManager::getInstance().GetPinName(String(dynamicConfigs[i].pin));
      out += "Pin:" + String(dynamicConfigs[i].pin);
      if (friendly.length() > 0)
        out += " (" + friendly + ")";
      out += " Int:" + String(dynamicConfigs[i].interval / 1000) + "s\n";
      out += "    [AUDIT] By:" + dynamicConfigs[i].updatedBy + " @ " +
             dynamicConfigs[i].lastUpdated + "\n";
    }
  }
  return out;
}

bool ScheduleManager::addDynamicTask(const String &name, const String &type,
                                     const String &pin, unsigned long interval,
                                     unsigned long duration,
                                     const String &source, bool enabled) {
  int slot = -1;
  // Use existing slot if name matches
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (activeDynamicTasks > 0 && dynamicConfigs[i].name == name) {
      slot = i;
      break;
    }
  }
  // Otherwise find empty slot
  if (slot == -1) {
    for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
      if (!tDynamicPool[i].isEnabled()) {
        slot = i;
        break;
      }
    }
  }

  if (slot != -1) {
    unsigned long interval_ms = interval * 1000;
    unsigned long duration_ms = duration * 1000;

    dynamicConfigs[slot].name = name;
    dynamicConfigs[slot].type = type;
    dynamicConfigs[slot].pin =
        CommandManager::getInstance().getPinFromName(pin);
    dynamicConfigs[slot].interval = interval_ms;
    dynamicConfigs[slot].duration = duration_ms;
    dynamicConfigs[slot].enabled = enabled;
    dynamicConfigs[slot].updatedBy = source;

    unsigned long uptimeS = millis() / 1000;
    dynamicConfigs[slot].lastUpdated =
        String(uptimeS / 3600) + "h " + String((uptimeS % 3600) / 60) + "m";

    tDynamicPool[slot].setInterval(interval_ms);
    if (enabled) {
      tDynamicPool[slot].enable();
    } else {
      tDynamicPool[slot].disable();
    }
    if (slot >= activeDynamicTasks)
      activeDynamicTasks++;
    return true;
  }
  return false;
}

bool ScheduleManager::removeDynamicTask(const String &name) {
  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (dynamicConfigs[i].name == name) {
      tDynamicPool[i].disable();
      dynamicConfigs[i].name = "";
      activeDynamicTasks--;
      return true;
    }
  }
  return false;
}

void ScheduleManager::clearDynamicTasks() {
  LOG_PRINTLN("SCHED: Clearing all dynamic tasks");
  for (int i = 0; i < activeDynamicTasks; i++) {
    tDynamicPool[i].disable();
  }
  activeDynamicTasks = 0;
}

void ScheduleManager::saveDynamicTasks() {
  JsonDocument doc;
  JsonArray schedules = doc["schedules"].to<JsonArray>();

  for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
    if (tDynamicPool[i].isEnabled()) {
      JsonObject obj = schedules.add<JsonObject>();
      obj["name"] = dynamicConfigs[i].name;
      obj["type"] = dynamicConfigs[i].type;
      obj["pin"] = String(dynamicConfigs[i].pin);
      obj["interval"] = dynamicConfigs[i].interval;
      obj["duration"] = dynamicConfigs[i].duration;
      obj["enabled"] = true;
      obj["updatedBy"] = dynamicConfigs[i].updatedBy;
      obj["lastUpdated"] = dynamicConfigs[i].lastUpdated;
    }
  }

  String json;
  serializeJson(doc, json);
  DataManager::getInstance().SaveSchedule(json);
  LOG_PRINTLN("SCHED: Dynamic tasks saved to FS with Audit info");
}

void ScheduleManager::processStreamLine(const String &line,
                                        CommInterface source) {
  // Expected format: name,type,pin,interval,duration (or tab-separated from
  // Excel)

  if (line.equalsIgnoreCase("END") || line.equalsIgnoreCase("STOP")) {
    isStreaming = false;
    LOG_PRINTLN("STREAM: Import Finished");
    return;
  }

  // Detect delimiter
  char delim = ',';
  if (line.indexOf('\t') != -1)
    delim = '\t';
  else if (line.indexOf(',') == -1) {
    if (!line.startsWith("NAME") && !line.startsWith("#")) {
      // Ignore lines that don't look like data
    }
    return;
  }

  // Skip headers
  if (line.startsWith("NAME") || line.startsWith("#"))
    return;

  String parts[6];
  int pIdx = 0;
  int pStart = 0;
  int pEnd = line.indexOf(delim);
  while (pIdx < 6) {
    if (pEnd == -1)
      pEnd = line.length();
    parts[pIdx++] = line.substring(pStart, pEnd);
    if (pEnd == (int)line.length())
      break;
    pStart = pEnd + 1;
    pEnd = line.indexOf(delim, pStart);
  }

  if (pIdx >= 4) {
    String name = parts[0];
    name.trim();
    String type = parts[1];
    type.trim();
    String pin = parts[2];
    pin.trim();
    unsigned long interval = parts[3].toInt(); // Now treated as seconds
    unsigned long duration =
        (pIdx > 4) ? parts[4].toInt() : 0; // Now treated as seconds
    bool enabled = (pIdx > 5)
                       ? (parts[5].trim(), parts[5].equalsIgnoreCase("true") ||
                                               parts[5].toInt() == 1)
                       : true;

    if (addDynamicTask(name, type, pin, interval, duration,
                       CommandManager::interfaceName(source), enabled)) {
      LOG_PRINTF("STREAM: Imported %s\n", name.c_str());
    }
  }
}
