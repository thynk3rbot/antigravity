#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include "../config.h"
#include <DHT.h>
#include <TaskSchedulerDeclarations.h>

class ScheduleManager {
public:
  static ScheduleManager &getInstance() {
    static ScheduleManager instance;
    return instance;
  }

  void init();
  void execute();

  // Task manipulation methods
  void set110VInterval(unsigned long interval_ms);
  void forceRelay110V(bool state);
  void trigger12VPulse();
  void triggerBlink();
  void triggerRestart(unsigned long delayMs);

  // JSON/CSV Dynamic Scheduling
  void loadDynamicSchedules();
  void loadSchedulesFromCsv(const String &csv);
  static void dynamicTaskCallback();

  struct DynamicTaskConfig {
    String name;
    String type;
    int pin;
    unsigned long interval;
    unsigned long duration;
    bool enabled;
  };

private:
  ScheduleManager();

  Scheduler runner;
  DHT dht;

  int dhtFailCount;
  bool safetyTripped;

  // Task Callbacks
  static void environmentalCallback();
  static void toggle110VCallback();
  static void pulse12VCallback();
  static void endPulse12VCallback();

  // System Callbacks
  static void loraTask();
  static void wifiTask();
  static void serialTask();
  static void heartbeatTask();
  static void buttonTask();
  static void displayTask();
  static void blinkTask();
  static void restartTask();
  static void bleTask();
  static void espNowTask();

  // Tasks
  Task tEnvironmental;
  Task t110V;
  Task t12V;
  Task t12VEnd;

  Task tLoRa;
  Task tWiFi;
  Task tSerial;
  Task tHeartbeat;
  Task tButton;
  Task tDisplay;
  Task tBlink;
  Task tRestart;
  Task tBLE;
  Task tESPNow;
  int blinkCount;

  // Dynamic Pool
  static const int MAX_DYNAMIC_TASKS = 5;
  Task tDynamicPool[MAX_DYNAMIC_TASKS];
  DynamicTaskConfig dynamicConfigs[MAX_DYNAMIC_TASKS];
  int activeDynamicTasks;

  void checkSafetyThreshold(float temp);
};

#endif // SCHEDULE_MANAGER_H
