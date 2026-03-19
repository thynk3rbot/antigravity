/**
 * @file schedule_manager.cpp
 * @brief Dynamic GPIO Scheduling implementation (v1 port)
 *
 * Ported from v1 ScheduleManager. Removed: DHT, MCPManager, LoRaManager,
 * sleep sequences, BLE/ESPNow tasks. Kept: dynamic task pool, TOGGLE/PULSE/
 * ON/OFF/PWM/READ, LittleFS persistence, audit trail.
 *
 * GPIO writes use direct digitalWrite/digitalRead (no I2C expander in v2).
 */

#include "schedule_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <TaskScheduler.h>
#include <esp32-hal-ledc.h>

// ============================================================================
// File-static scheduler state (keeps TaskScheduler.h out of the public header)
// ============================================================================

static Scheduler       s_runner;
static std::vector<Task*> s_pool;

// Class-static config vector (declared in header, defined here)
std::vector<ScheduleManager::DynamicTaskConfig> ScheduleManager::s_configs;

static constexpr const char* SCHED_FILE = "/schedule.json";

// ============================================================================
// Lifecycle
// ============================================================================

bool ScheduleManager::init() {
    Serial.println("[SCHED] Initializing scheduler");

    if (!LittleFS.begin(true)) {
        Serial.println("[SCHED] LittleFS mount failed — schedules will not persist");
        // Continue without persistence; tasks can still be added at runtime
    } else {
        Serial.println("[SCHED] LittleFS mounted");
        loadSchedules();
    }

    s_runner.init();
    return true;
}

void ScheduleManager::execute() {
    s_runner.execute();
}

// ============================================================================
// Persistence
// ============================================================================

void ScheduleManager::loadSchedules() {
    if (!LittleFS.exists(SCHED_FILE)) {
        Serial.println("[SCHED] No schedule file found");
        return;
    }

    File file = LittleFS.open(SCHED_FILE, "r");
    if (!file) {
        Serial.println("[SCHED] Failed to open schedule file");
        return;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[SCHED] JSON parse error: %s\n", err.c_str());
        return;
    }

    clearTasks();

    JsonArray schedules = doc["schedules"].as<JsonArray>();
    for (JsonObject s : schedules) {
        DynamicTaskConfig cfg;
        cfg.name      = s["name"]     | "Task";
        cfg.type      = s["type"]     | "TOGGLE";
        cfg.pin       = s["pin"]      | -1;
        cfg.interval  = (unsigned long)(s["interval"]  | 0UL);
        cfg.duration  = (unsigned long)(s["duration"]  | 0UL);
        cfg.enabled   = s["enabled"]  | true;
        cfg.value     = s["value"]    | 0;
        cfg.updatedBy = s["updatedBy"]   | "SAVED";
        cfg.lastUpdated = s["lastUpdated"] | "---";

        if (cfg.pin < 0 || cfg.interval == 0) continue;

        Task* t = new Task(cfg.interval, TASK_FOREVER,
                           &ScheduleManager::dynamicTaskCallback,
                           &s_runner, cfg.enabled);

        setupPin(cfg.pin, cfg.type, cfg.value);
        s_pool.push_back(t);
        s_configs.push_back(cfg);

        Serial.printf("[SCHED] Loaded: %s (%s) pin=%d int=%lums\n",
                      cfg.name.c_str(), cfg.type.c_str(), cfg.pin, cfg.interval);
    }

    Serial.printf("[SCHED] Loaded %d task(s)\n", (int)s_configs.size());
}

void ScheduleManager::saveSchedules() {
    DynamicJsonDocument doc(4096);
    JsonArray schedules = doc["schedules"].to<JsonArray>();

    for (size_t i = 0; i < s_configs.size(); i++) {
        const DynamicTaskConfig& cfg = s_configs[i];
        JsonObject obj = schedules.createNestedObject();
        obj["name"]        = cfg.name;
        obj["type"]        = cfg.type;
        obj["pin"]         = cfg.pin;
        obj["interval"]    = cfg.interval;
        obj["duration"]    = cfg.duration;
        obj["enabled"]     = s_pool[i]->isEnabled();
        obj["value"]       = cfg.value;
        obj["updatedBy"]   = cfg.updatedBy;
        obj["lastUpdated"] = cfg.lastUpdated;
    }

    File file = LittleFS.open(SCHED_FILE, "w");
    if (!file) {
        Serial.println("[SCHED] Failed to open schedule file for write");
        return;
    }
    serializeJson(doc, file);
    file.close();

    Serial.printf("[SCHED] Saved %d task(s) to %s\n",
                  (int)s_configs.size(), SCHED_FILE);
}

// ============================================================================
// Task Management
// ============================================================================

bool ScheduleManager::addTask(const String& name, const String& type, int pin,
                               unsigned long interval_s, unsigned long duration_s,
                               const String& source, bool enabled, int value) {
    unsigned long interval_ms = interval_s * 1000UL;
    unsigned long duration_ms = duration_s * 1000UL;

    // Update existing task if name collides
    for (size_t i = 0; i < s_configs.size(); i++) {
        if (s_configs[i].name == name) {
            s_configs[i].type       = type;
            s_configs[i].pin        = pin;
            s_configs[i].interval   = interval_ms;
            s_configs[i].duration   = duration_ms;
            s_configs[i].enabled    = enabled;
            s_configs[i].updatedBy  = source;
            s_configs[i].value      = value;
            s_configs[i].pulseActive = false;
            s_configs[i].lastUpdated = uptimeStr();

            s_pool[i]->setInterval(interval_ms > 0 ? interval_ms : 1000);
            enabled ? s_pool[i]->enable() : s_pool[i]->disable();

            setupPin(pin, type, value);
            Serial.printf("[SCHED] Updated task '%s'\n", name.c_str());
            return true;
        }
    }

    if (interval_ms == 0) {
        Serial.println("[SCHED] Interval cannot be 0 for new task");
        return false;
    }

    if (ESP.getFreeHeap() < 4096) {
        Serial.println("[SCHED] Low heap — cannot add task");
        return false;
    }

    DynamicTaskConfig cfg;
    cfg.name        = name;
    cfg.type        = type;
    cfg.pin         = pin;
    cfg.interval    = interval_ms;
    cfg.duration    = duration_ms;
    cfg.enabled     = enabled;
    cfg.updatedBy   = source;
    cfg.value       = value;
    cfg.pulseActive = false;
    cfg.pulseStart  = 0;
    cfg.lastUpdated = uptimeStr();

    Task* t = new Task(interval_ms, TASK_FOREVER,
                       &ScheduleManager::dynamicTaskCallback,
                       &s_runner, enabled);

    setupPin(pin, type, value);
    s_pool.push_back(t);
    s_configs.push_back(cfg);

    Serial.printf("[SCHED] Added task '%s' (%s) pin=%d int=%lus [total=%d heap=%d]\n",
                  name.c_str(), type.c_str(), pin, interval_s,
                  (int)s_configs.size(), (int)ESP.getFreeHeap());
    return true;
}

bool ScheduleManager::removeTask(const String& name) {
    for (size_t i = 0; i < s_configs.size(); i++) {
        if (s_configs[i].name == name) {
            s_runner.deleteTask(*s_pool[i]);
            delete s_pool[i];
            s_pool.erase(s_pool.begin() + i);
            s_configs.erase(s_configs.begin() + i);
            Serial.printf("[SCHED] Removed task '%s' [total=%d]\n",
                          name.c_str(), (int)s_configs.size());
            return true;
        }
    }
    return false;
}

void ScheduleManager::clearTasks() {
    for (Task* t : s_pool) {
        s_runner.deleteTask(*t);
        delete t;
    }
    s_pool.clear();
    s_configs.clear();
    Serial.println("[SCHED] All tasks cleared");
}

bool ScheduleManager::enableTask(const String& name) {
    for (size_t i = 0; i < s_configs.size(); i++) {
        if (s_configs[i].name == name) {
            s_pool[i]->enable();
            s_configs[i].enabled = true;
            return true;
        }
    }
    return false;
}

bool ScheduleManager::disableTask(const String& name) {
    for (size_t i = 0; i < s_configs.size(); i++) {
        if (s_configs[i].name == name) {
            s_pool[i]->disable();
            s_configs[i].enabled = false;
            return true;
        }
    }
    return false;
}

// ============================================================================
// Reporting
// ============================================================================

String ScheduleManager::getReport() {
    String out = "--- SCHEDULES (" + String(s_configs.size()) +
                 ", heap=" + String(ESP.getFreeHeap()) + ") ---\n";

    for (size_t i = 0; i < s_configs.size(); i++) {
        const DynamicTaskConfig& cfg = s_configs[i];
        out += "[" + String(i) + "] " + cfg.name +
               " (" + cfg.type + ") pin=" + String(cfg.pin) +
               " int=" + String(cfg.interval / 1000) + "s";
        if (cfg.duration > 0)
            out += " dur=" + String(cfg.duration / 1000) + "s";
        out += " " + String(s_pool[i]->isEnabled() ? "ON" : "OFF");
        out += "\n    by:" + cfg.updatedBy + " @ " + cfg.lastUpdated + "\n";
    }
    return out;
}

void ScheduleManager::getJson(JsonDocument& doc) {
    JsonArray schedules = doc["schedules"].to<JsonArray>();
    for (size_t i = 0; i < s_configs.size(); i++) {
        const DynamicTaskConfig& cfg = s_configs[i];
        JsonObject obj = schedules.createNestedObject();
        obj["name"]        = cfg.name;
        obj["type"]        = cfg.type;
        obj["pin"]         = cfg.pin;
        obj["interval_s"]  = cfg.interval / 1000;
        obj["duration_s"]  = cfg.duration / 1000;
        obj["enabled"]     = s_pool[i]->isEnabled();
        obj["value"]       = cfg.value;
        obj["state"]       = (cfg.pin >= 0) ? (int)digitalRead(cfg.pin) : -1;
        obj["updatedBy"]   = cfg.updatedBy;
        obj["lastUpdated"] = cfg.lastUpdated;
    }
}

int ScheduleManager::getTaskCount() {
    return (int)s_configs.size();
}

// ============================================================================
// Task Execution Callback
// ============================================================================

void ScheduleManager::dynamicTaskCallback() {
    Task& t = s_runner.currentTask();

    // Find config that owns this Task* by pointer comparison
    int idx = -1;
    for (int i = 0; i < (int)s_pool.size(); i++) {
        if (s_pool[i] == &t) { idx = i; break; }
    }
    if (idx < 0) return;

    DynamicTaskConfig& cfg = s_configs[idx];

    if (cfg.type == "TOGGLE") {
        bool cur = digitalRead(cfg.pin);
        digitalWrite(cfg.pin, !cur);
        Serial.printf("[SCHED] %s TOGGLE -> %d\n", cfg.name.c_str(), !cur);

    } else if (cfg.type == "PULSE") {
        if (!cfg.pulseActive) {
            digitalWrite(cfg.pin, HIGH);
            cfg.pulseStart  = millis();
            cfg.pulseActive = true;
        } else if (millis() - cfg.pulseStart >= (cfg.duration > 0 ? cfg.duration : 5000)) {
            digitalWrite(cfg.pin, LOW);
            cfg.pulseActive = false;
        }

    } else if (cfg.type == "ON") {
        digitalWrite(cfg.pin, HIGH);
        Serial.printf("[SCHED] %s ON pin=%d\n", cfg.name.c_str(), cfg.pin);

    } else if (cfg.type == "OFF") {
        digitalWrite(cfg.pin, LOW);
        Serial.printf("[SCHED] %s OFF pin=%d\n", cfg.name.c_str(), cfg.pin);

    } else if (cfg.type == "PWM") {
        uint8_t ch = (uint8_t)(cfg.pin % 8);
        ledcWrite(ch, (uint32_t)cfg.value);

    } else if (cfg.type == "READ") {
        int raw = analogRead(cfg.pin);
        Serial.printf("[SCHED] %s READ pin=%d -> %d\n", cfg.name.c_str(), cfg.pin, raw);
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void ScheduleManager::setupPin(int pin, const String& type, int value) {
    if (pin < 0) return;

    if (type == "TOGGLE" || type == "PULSE" || type == "ON" || type == "OFF") {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);

    } else if (type == "PWM") {
        uint8_t ch = (uint8_t)(pin % 8);
        ledcSetup(ch, 5000, 8);
        ledcAttachPin(pin, ch);
        ledcWrite(ch, (uint32_t)value);

    } else if (type == "READ") {
        pinMode(pin, ANALOG);
    }
}

String ScheduleManager::uptimeStr() {
    unsigned long s = millis() / 1000;
    return String(s / 3600) + "h " + String((s % 3600) / 60) + "m";
}
