#include "ScheduleManager.h"
#include "../utils/DebugMacros.h"
#include "BLEManager.h"
#include "CommandManager.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "ESPNowManager.h"
#include "LoRaManager.h"
#include "MCPManager.h"
#include "WiFiManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <TaskScheduler.h>
#include <esp32-hal-ledc.h>

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
      tLoRa(10, TASK_FOREVER, &ScheduleManager::loraTask), // ISR: 500→10ms
      tWiFi(1000, TASK_FOREVER, &ScheduleManager::wifiTask),
      tSerial(20, TASK_FOREVER, &ScheduleManager::serialTask), // ISR: 100→20ms
      tHeartbeat(300000, TASK_FOREVER, &ScheduleManager::heartbeatTask),
      // tButton removed — replaced by attachInterrupt(buttonISR) in init()
      tDisplay(2000, TASK_FOREVER, &ScheduleManager::displayTask),
      tBlink(200, 20, &ScheduleManager::blinkTask),
      tRestart(1000, TASK_ONCE, &ScheduleManager::restartTask),
      tBLE(20, TASK_FOREVER, &ScheduleManager::bleTask),
      tESPNow(50, TASK_FOREVER, &ScheduleManager::espNowTask),
      tPeripheralSerial(20, TASK_FOREVER,
                        &ScheduleManager::peripheralSerialTask),
      tBatteryMonitor(300000, TASK_FOREVER,
                      &ScheduleManager::batteryMonitorCallback),
      blinkCount(0) {
  instance_ptr = this;
}

ScheduleManager::~ScheduleManager() {
  // Free heap-allocated Task objects
  for (Task *t : tDynamicPool) {
    delete t;
  }
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
  // tButton NOT added — replaced by hardware ISR below
  runner.addTask(tDisplay);
  runner.addTask(tBlink);
  runner.addTask(tRestart);
  runner.addTask(tBLE);
  runner.addTask(tESPNow);
  runner.addTask(tPeripheralSerial);
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
  // Button ISR — replaces 50ms tButton poll
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_PRG),
                  ScheduleManager::buttonISR, FALLING);
  LOG_PRINTLN("SCHED: Button ISR attached (FALLING edge)");
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

  // Peripheral Serial task (Serial1 on G47/G48)
  Serial1.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  tPeripheralSerial.enable();

  // Battery protection monitor
  tBatteryMonitor.enableDelayed(10000);

  Serial.setTimeout(5);
  Serial1.setTimeout(5);

  loadDynamicSchedules();
}

void IRAM_ATTR ScheduleManager::globalInterruptHandler(void *arg) {
  if (!instance_ptr)
    return;
  Task *t = (Task *)arg;
  t->forceNextIteration();
}

// Button ISR: fires on FALLING edge of PRG button, sets flag for displayTask
void IRAM_ATTR ScheduleManager::buttonISR() {
  if (instance_ptr) {
    instance_ptr->_btnPressed = true;
  }
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
  LoRaManager::getInstance().periodicTick();
}

void ScheduleManager::wifiTask() { WiFiManager::getInstance().handle(); }

void ScheduleManager::serialTask() {
  ScheduleManager &inst = getInstance();
  static String serialBuffer = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || (c == '\r' && Serial.peek() != '\n')) {
      String line = serialBuffer;
      line.trim();
      serialBuffer = "";
      if (line.length() > 0) {
        if (inst.isStreaming) {
          inst.processStreamLine(line, CommInterface::COMM_SERIAL);
        } else {
          CommandManager::getInstance().handleCommand(
              line, CommInterface::COMM_SERIAL);
        }
      }
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }
}

void ScheduleManager::peripheralSerialTask() {
  static String pSerialBuffer = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || (c == '\r' && Serial1.peek() != '\n')) {
      String line = pSerialBuffer;
      line.trim();
      pSerialBuffer = "";
      if (line.length() > 0) {
        CommandManager::getInstance().handleCommand(line,
                                                    CommInterface::COMM_SERIAL);
      }
    } else if (c != '\r') {
      pSerialBuffer += c;
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

// buttonTask REMOVED — replaced by buttonISR() hardware interrupt
// The FALLING edge ISR sets instance_ptr->_btnPressed = true
// displayTask checks and resets this flag every 2s

void ScheduleManager::displayTask() {
  DisplayManager &disp = DisplayManager::getInstance();
  // Absorb button press from ISR (flag set in buttonISR, cleared here)
  if (instance_ptr && instance_ptr->_btnPressed) {
    instance_ptr->_btnPressed = false;
    disp.SetDisplayActive(true);
    disp.NextPage();
    LOG_PRINTLN("BTN: PRG Pressed (ISR) - Paging...");
  }
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

  // Clear existing dynamic tasks first
  clearDynamicTasks();

  JsonArray schedules = doc["schedules"].as<JsonArray>();
  for (JsonObject s : schedules) {
    DynamicTaskConfig cfg;
    cfg.name = s["name"] | "Task";
    cfg.type = s["type"] | "TOGGLE";
    cfg.pin = CommandManager::getInstance().getPinFromName(s["pin"] | "");
    cfg.interval = (unsigned long)(s["interval"] | 0) * 1000;
    cfg.duration = (unsigned long)(s["duration"] | 0) * 1000;
    cfg.enabled = s["enabled"] | true;
    cfg.value = s["value"] | 0;

    cfg.triggerPin =
        s.containsKey("triggerPin")
            ? CommandManager::getInstance().getPinFromName(s["triggerPin"] | "")
            : -1;
    cfg.triggerMode = s["triggerMode"] | 0;
    cfg.threshold = s["threshold"] | 0;
    cfg.thresholdGreater = s["thresholdGreater"] | true;
    cfg.notifyCmd = s["notifyCmd"] | "";

    cfg.updatedBy = s["updatedBy"] | "UNKNOWN";
    cfg.lastUpdated = s["lastUpdated"] | "---";

    if ((cfg.pin >= 0 || cfg.type == "LORA_TX") &&
        (cfg.interval > 0 || cfg.triggerPin >= 0)) {
      // Heap-allocate a new Task
      Task *t = new Task(cfg.interval > 0 ? cfg.interval : 1000, TASK_FOREVER,
                         &ScheduleManager::dynamicTaskCallback, &runner, false);

      if (cfg.triggerPin >= 0 && cfg.triggerMode > 0) {
        pinMode(cfg.triggerPin, INPUT_PULLUP);
        int mode = (cfg.triggerMode == 1)
                       ? RISING
                       : (cfg.triggerMode == 2 ? FALLING : CHANGE);
        attachInterruptArg(cfg.triggerPin,
                           ScheduleManager::globalInterruptHandler, t, mode);
        if (cfg.interval == 0)
          t->disable(); // Only runs on interrupt
        else if (cfg.enabled)
          t->enable();
      } else if (cfg.enabled) {
        t->enable();
      }

      setupDynamicPin(cfg.pin, cfg.type, cfg.value);

      tDynamicPool.push_back(t);
      dynamicConfigs.push_back(cfg);
      LOG_PRINTF("SCHED: Loaded task %s (%s) pin=%d trig=%d\n",
                 cfg.name.c_str(), cfg.type.c_str(), cfg.pin, cfg.triggerPin);
    }
  }
  LOG_PRINTF("SCHED: Loaded %d dynamic tasks\n", (int)dynamicConfigs.size());
}

void ScheduleManager::dynamicTaskCallback() {
  if (!instance_ptr)
    return;
  Task &t = instance_ptr->runner.currentTask();

  // Find which config owns this Task* by pointer comparison
  int idx = -1;
  for (int i = 0; i < (int)instance_ptr->tDynamicPool.size(); i++) {
    if (instance_ptr->tDynamicPool[i] == &t) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return;

  DynamicTaskConfig &cfg = instance_ptr->dynamicConfigs[idx];

  // THRESHOLD Trigger Check
  if (cfg.threshold > 0) {
    int val = analogRead(cfg.pin);
    bool triggered =
        cfg.thresholdGreater ? (val > cfg.threshold) : (val < cfg.threshold);
    if (!triggered)
      return; // Skip execution if threshold not met
  }

  if (cfg.type == "TOGGLE") {
    bool cur = MCPManager::readPin(cfg.pin);
    MCPManager::writePin(cfg.pin, !cur);
    LOG_PRINTF("SCHED: %s Toggle -> %d\n", cfg.name.c_str(), !cur);

  } else if (cfg.type == "PULSE") {
    // Per-config pulse state — no static arrays needed
    if (!cfg.pulseActive) {
      MCPManager::writePin(cfg.pin, true);
      cfg.pulseStart = millis();
      cfg.pulseActive = true;
    } else if (millis() - cfg.pulseStart >=
               (cfg.duration > 0 ? cfg.duration : 5000)) {
      MCPManager::writePin(cfg.pin, false);
      cfg.pulseActive = false;
    }

  } else if (cfg.type == "PWM") {
    // PWM set on setup or update to avoid ledcSetup duty cycle reset/flicker
    uint8_t ch = cfg.pin % 8;
    ledcWrite(ch, (uint32_t)cfg.value);
    // LOG_PRINTF("SCHED: %s PWM -> %d\n", cfg.name.c_str(), cfg.value);

  } else if (cfg.type == "SERVO") {
    uint8_t ch = 8 + (cfg.pin % 8);
    uint32_t duty_us = 544 + (uint32_t)(cfg.value * (2400 - 544) / 180);
    ledcWrite(ch, (uint32_t)(duty_us * 65535UL / 20000UL));
    // LOG_PRINTF("SCHED: %s SERVO -> %d deg\n", cfg.name.c_str(), cfg.value);

  } else if (cfg.type == "READ") {
    int raw = analogRead(cfg.pin);
    LOG_PRINTF("SCHED: %s READ pin=%d -> %d\n", cfg.name.c_str(), cfg.pin, raw);
  } else if (cfg.type == "LORA_TX") {
    String payload = cfg.name + " " + String(cfg.value);
    LoRaManager::getInstance().SendLoRa(payload);
    LOG_PRINTF("SCHED: %s LORA_TX -> %s\n", cfg.name.c_str(), payload.c_str());
  } else if (cfg.type == "ALERT") {
    int raw = MCPManager::isMcpPin(cfg.pin) ? (int)MCPManager::readPin(cfg.pin)
                                            : analogRead(cfg.pin);
    bool triggered =
        cfg.thresholdGreater ? (raw > cfg.threshold) : (raw < cfg.threshold);
    if (triggered && cfg.notifyCmd.length() > 0) {
      CommandManager::getInstance().handleCommand(cfg.notifyCmd,
                                                  CommInterface::COMM_INTERNAL);
      LOG_PRINTF("SCHED: %s ALERT triggered (raw=%d) -> %s\n", cfg.name.c_str(),
                 raw, cfg.notifyCmd.c_str());
    }
  }
}

void ScheduleManager::setupDynamicPin(int pin, const String &type, int value) {
  if (pin < 0)
    return;
  if (type == "TOGGLE" || type == "PULSE") {
    MCPManager::setupPin(pin, OUTPUT);
  } else if (type == "PWM") {
    if (!MCPManager::isMcpPin(pin)) {
      uint8_t ch = pin % 8;
      ledcSetup(ch, 5000, 8);
      ledcAttachPin(pin, ch);
      ledcWrite(ch, (uint32_t)value);
    }
  } else if (type == "SERVO") {
    if (!MCPManager::isMcpPin(pin)) {
      uint8_t ch = 8 + (pin % 8);
      ledcSetup(ch, 50, 16);
      ledcAttachPin(pin, ch);
      uint32_t duty_us = 544 + (uint32_t)(value * (2400 - 544) / 180);
      ledcWrite(ch, (uint32_t)(duty_us * 65535UL / 20000UL));
    }
  } else if (type == "READ") {
    if (!MCPManager::isMcpPin(pin))
      pinMode(pin, ANALOG);
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
  if (bat > 0.5f && bat < 3.20f) {
    LOG_PRINTLN("SCHED: Low Battery! Routing through executeSleep.");
    CommandManager::executeSleep(6.0f, "LOW-BAT:" + String(bat, 2) + "V");
  }
}

void ScheduleManager::getTaskJson(JsonDocument &doc) {
  JsonArray schedules = doc["schedules"].to<JsonArray>();
  for (size_t i = 0; i < dynamicConfigs.size(); i++) {
    const DynamicTaskConfig &cfg = dynamicConfigs[i];
    if (tDynamicPool[i]->isEnabled() || cfg.name.length() > 0) {
      JsonObject obj = schedules.add<JsonObject>();
      obj["name"] = cfg.name;
      obj["type"] = cfg.type;
      obj["pin"] = String(cfg.pin);
      String friendly = DataManager::getInstance().GetPinName(String(cfg.pin));
      if (friendly.length() > 0)
        obj["pinName"] = friendly;
      obj["interval"] = cfg.interval / 1000;
      obj["duration"] = cfg.duration / 1000;
      obj["value"] = cfg.value;
      obj["enabled"] = cfg.enabled;
      obj["state"] = (cfg.pin >= 0) ? digitalRead(cfg.pin) : 0;
      obj["nextRun"] = runner.timeUntilNextIteration(*tDynamicPool[i]) / 1000;
      obj["updatedBy"] = cfg.updatedBy;
      obj["lastUpdated"] = cfg.lastUpdated;
      if (cfg.triggerPin >= 0) {
        obj["triggerPin"] = String(cfg.triggerPin);
        obj["triggerMode"] = cfg.triggerMode;
      }
      if (cfg.threshold > 0) {
        obj["threshold"] = cfg.threshold;
        obj["thresholdGreater"] = cfg.thresholdGreater;
      }
    }
  }
}

String ScheduleManager::getTaskReport() {
  String out = "--- SYSTEM TASKS ---\n";
  Task *tasks[] = {&tLoRa, &tWiFi,   &tSerial,        &tHeartbeat,
                   &tBLE,  &tESPNow, &tDisplay,       &t110V,
                   &t12V,  &tBlink,  &tBatteryMonitor};
  const char *names[] = {"LoRa",      "WiFi",    "Serial",  "HEARTBEAT",
                         "BLE",       "ESP-NOW", "Display", "110V",
                         "12V_Pulse", "Blink",   "Battery"};
  for (int i = 0; i < 11; i++) {
    out += String(names[i]) + ": " + (tasks[i]->isEnabled() ? "ON" : "OFF");
    out += " (" + String(tasks[i]->getInterval() / 1000.0, 1) + "s)\n";
  }

  out += "\n--- DYNAMIC SCHEDULES (" + String(dynamicConfigs.size()) +
         ", heap=" + String(ESP.getFreeHeap()) + ") ---\n";
  for (size_t i = 0; i < dynamicConfigs.size(); i++) {
    const DynamicTaskConfig &cfg = dynamicConfigs[i];
    if (tDynamicPool[i]->isEnabled()) {
      out += "[" + String(i) + "] " + cfg.name + " (" + cfg.type + ") ";
      String friendly = DataManager::getInstance().GetPinName(String(cfg.pin));
      out += "Pin:" + String(cfg.pin);
      if (friendly.length() > 0)
        out += " (" + friendly + ")";
      out += " Int:" + String(cfg.interval / 1000) + "s\n";
      out += "    [AUDIT] By:" + cfg.updatedBy + " @ " + cfg.lastUpdated + "\n";
    }
  }
  return out;
}

bool ScheduleManager::addDynamicTask(const String &name, const String &type,
                                     const String &pin, unsigned long interval,
                                     unsigned long duration,
                                     const String &source, bool enabled,
                                     int value, int triggerPin, int triggerMode,
                                     int threshold, bool thresholdGreater,
                                     const String &notifyCmd) {
  unsigned long interval_ms = interval * 1000;
  unsigned long duration_ms = duration * 1000;
  int resolvedPin = CommandManager::getInstance().getPinFromName(pin);

  // Update existing task if name collision
  for (size_t i = 0; i < dynamicConfigs.size(); i++) {
    if (dynamicConfigs[i].name == name) {
      dynamicConfigs[i].type = type;
      dynamicConfigs[i].pin = resolvedPin;
      dynamicConfigs[i].interval = interval_ms;
      dynamicConfigs[i].duration = duration_ms;
      dynamicConfigs[i].enabled = enabled;
      dynamicConfigs[i].updatedBy = source;
      dynamicConfigs[i].value = value;
      dynamicConfigs[i].triggerPin = triggerPin;
      dynamicConfigs[i].triggerMode = triggerMode;
      dynamicConfigs[i].threshold = threshold;
      dynamicConfigs[i].thresholdGreater = thresholdGreater;
      dynamicConfigs[i].notifyCmd = notifyCmd;
      dynamicConfigs[i].pulseActive = false;
      unsigned long up = millis() / 1000;
      dynamicConfigs[i].lastUpdated =
          String(up / 3600) + "h " + String((up % 3600) / 60) + "m";

      tDynamicPool[i]->setInterval(interval_ms > 0 ? interval_ms : 1000);

      if (triggerPin >= 0 && triggerMode > 0) {
        pinMode(triggerPin, INPUT_PULLUP);
        int mode =
            (triggerMode == 1) ? RISING : (triggerMode == 2 ? FALLING : CHANGE);
        detachInterrupt(triggerPin);
        attachInterruptArg(triggerPin, ScheduleManager::globalInterruptHandler,
                           tDynamicPool[i], mode);
        if (interval_ms == 0)
          tDynamicPool[i]->disable();
        else if (enabled)
          tDynamicPool[i]->enable();
      } else {
        enabled ? tDynamicPool[i]->enable() : tDynamicPool[i]->disable();
      }

      setupDynamicPin(resolvedPin, type, value);

      LOG_PRINTF("SCHED: Updated task '%s'\n", name.c_str());
      return true;
    }
  }

  // Check available heap before allocating
  if (ESP.getFreeHeap() < 4096) {
    LOG_PRINTLN("SCHED: Low heap — cannot add task");
    return false;
  }

  // New task — heap allocate and register
  DynamicTaskConfig cfg;
  cfg.name = name;
  cfg.type = type;
  cfg.pin = resolvedPin;
  cfg.interval = interval_ms;
  cfg.duration = duration_ms;
  cfg.enabled = enabled;
  cfg.updatedBy = source;
  cfg.value = value;
  cfg.triggerPin = triggerPin;
  cfg.triggerMode = triggerMode;
  cfg.threshold = threshold;
  cfg.thresholdGreater = thresholdGreater;
  cfg.notifyCmd = notifyCmd;
  unsigned long up = millis() / 1000;
  cfg.lastUpdated = String(up / 3600) + "h " + String((up % 3600) / 60) + "m";

  Task *t = new Task(interval_ms > 0 ? interval_ms : 1000, TASK_FOREVER,
                     &ScheduleManager::dynamicTaskCallback, &runner, false);

  if (triggerPin >= 0 && triggerMode > 0) {
    pinMode(triggerPin, INPUT_PULLUP);
    int mode =
        (triggerMode == 1) ? RISING : (triggerMode == 2 ? FALLING : CHANGE);
    attachInterruptArg(triggerPin, ScheduleManager::globalInterruptHandler, t,
                       mode);
    if (interval_ms == 0)
      t->disable();
    else if (enabled)
      t->enable();
  } else if (enabled) {
    t->enable();
  }

  setupDynamicPin(resolvedPin, type, value);

  tDynamicPool.push_back(t);
  dynamicConfigs.push_back(cfg);

  LOG_PRINTF("SCHED: Added task '%s' (%s) pin=%d [total=%d, heap=%d]\n",
             name.c_str(), type.c_str(), resolvedPin,
             (int)dynamicConfigs.size(), (int)ESP.getFreeHeap());
  return true;
}

bool ScheduleManager::removeDynamicTask(const String &name) {
  for (size_t i = 0; i < dynamicConfigs.size(); i++) {
    if (dynamicConfigs[i].name == name) {
      runner.deleteTask(*tDynamicPool[i]);
      delete tDynamicPool[i];
      tDynamicPool.erase(tDynamicPool.begin() + i);
      dynamicConfigs.erase(dynamicConfigs.begin() + i);
      LOG_PRINTF("SCHED: Removed task '%s' [total=%d]\n", name.c_str(),
                 (int)dynamicConfigs.size());
      return true;
    }
  }
  return false;
}

void ScheduleManager::clearDynamicTasks() {
  LOG_PRINTLN("SCHED: Clearing all dynamic tasks");
  for (Task *t : tDynamicPool) {
    runner.deleteTask(*t);
    delete t;
  }
  tDynamicPool.clear();
  dynamicConfigs.clear();
  LOG_PRINTLN("SCHED: All dynamic tasks cleared");
}

void ScheduleManager::saveDynamicTasks() {
  JsonDocument doc;
  JsonArray schedules = doc["schedules"].to<JsonArray>();

  for (size_t i = 0; i < dynamicConfigs.size(); i++) {
    const DynamicTaskConfig &cfg = dynamicConfigs[i];
    JsonObject obj = schedules.add<JsonObject>();
    obj["name"] = cfg.name;
    obj["type"] = cfg.type;
    obj["pin"] = String(cfg.pin);
    obj["interval"] = cfg.interval;
    obj["duration"] = cfg.duration;
    obj["enabled"] = tDynamicPool[i]->isEnabled();
    obj["value"] = cfg.value;
    obj["updatedBy"] = cfg.updatedBy;
    obj["lastUpdated"] = cfg.lastUpdated;
    if (cfg.triggerPin >= 0)
      obj["triggerPin"] = String(cfg.triggerPin);
    if (cfg.triggerMode != 0)
      obj["triggerMode"] = cfg.triggerMode;
    if (cfg.threshold != 0)
      obj["threshold"] = cfg.threshold;
    if (!cfg.thresholdGreater)
      obj["thresholdGreater"] = false;
    if (cfg.notifyCmd.length() > 0)
      obj["notifyCmd"] = cfg.notifyCmd;
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
