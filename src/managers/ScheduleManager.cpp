#define _TASK_SLEEP_ON_IDLE_RUN
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
      tESPNow(50, TASK_FOREVER, &ScheduleManager::espNowTask), blinkCount(0) {
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
  tBLE.enable();
  tLoRa.enableDelayed(2000);
  tWiFi.enableDelayed(3000);
  tHeartbeat.enableDelayed(8000);

  // ESP-NOW task
  if (DataManager::getInstance().espNowEnabled) {
    tESPNow.enable();
  }

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
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    if (input.length() > 0 && input.length() < 128) {
      CommandManager::getInstance().handleCommand(input,
                                                  CommInterface::COMM_SERIAL);
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
    DataManager::getInstance().LogMessage("BLE> " + cmd);
    CommandManager::getInstance().handleCommand(cmd, CommInterface::COMM_BLE);
  }
}

void ScheduleManager::espNowTask() {
  String msg;
  while (ESPNowManager::getInstance().poll(msg)) {
    DataManager::getInstance().LogMessage("EN> " + msg);
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

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    LOG_PRINTLN("SCHED: JSON Parse Fail");
    return;
  }

  JsonArray schedules = doc["schedules"];
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

  StaticJsonDocument<2048> doc;
  JsonArray schedules = doc.createNestedArray("schedules");

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
      JsonObject obj = schedules.createNestedObject();
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
