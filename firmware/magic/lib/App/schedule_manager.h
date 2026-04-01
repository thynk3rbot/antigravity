/**
 * @file schedule_manager.h
 * @brief Dynamic GPIO Scheduling — ported from v1 ScheduleManager
 *
 * Manages a pool of named, persistent scheduled GPIO tasks.
 * Backed by TaskScheduler library; state persisted to LittleFS (/schedule.json).
 *
 * Command interface (via CommandManager):
 *   SCHED LIST
 *   SCHED ADD <name> <type> <pin> <interval_s> [duration_s]
 *   SCHED REM  <name>
 *   SCHED CLEAR
 *   SCHED SAVE
 *   SCHED ENABLE  <name>
 *   SCHED DISABLE <name>
 *
 * Task types:
 *   TOGGLE  — flip pin state every interval
 *   PULSE   — set pin HIGH for duration_s, then LOW, repeat every interval
 *   ON      — set pin HIGH once (no repeat)
 *   OFF     — set pin LOW once (no repeat)
 *   PWM     — write PWM duty (value 0-255) via LEDC every interval
 *   READ    — log analogRead every interval
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

class ScheduleManager {
public:
    // ========================================================================
    // Task Configuration (persistent state per schedule entry)
    // ========================================================================

    struct DynamicTaskConfig {
        String        name;
        String        type;          // TOGGLE, PULSE, ON, OFF, PWM, READ
        int           pin;
        unsigned long interval;      // milliseconds between firings
        unsigned long duration;      // milliseconds HIGH for PULSE
        bool          enabled;
        String        updatedBy;     // who last modified (CMD, HTTP, BLE, INTERNAL)
        String        lastUpdated;   // uptime string at last modification
        int           value;         // PWM duty 0-255; unused for other types

        // Pulse state tracking (runtime only, not persisted)
        bool          pulseActive;
        unsigned long pulseStart;
    };

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize TaskScheduler, mount LittleFS, load saved schedules
     * @return true if initialization succeeded
     */
    static bool init();

    /**
     * @brief Advance the TaskScheduler — call from a FreeRTOS task every 50ms
     */
    static void execute();

    // ========================================================================
    // Schedule Persistence
    // ========================================================================

    /**
     * @brief Load tasks from /schedule.json on LittleFS
     */
    static void loadSchedules();

    /**
     * @brief Save current task pool to /schedule.json on LittleFS
     */
    static void saveSchedules();

    // ========================================================================
    // Task Management
    // ========================================================================

    /**
     * @brief Add or update a named scheduled task
     * @param name      Unique task identifier
     * @param type      Task type string (TOGGLE/PULSE/ON/OFF/PWM/READ)
     * @param pin       GPIO pin number
     * @param interval_s Firing interval in seconds (0 = one-shot)
     * @param duration_s PULSE on-time in seconds (ignored for other types)
     * @param source    Audit label for who added this task
     * @param enabled   Whether task starts enabled
     * @param value     PWM duty cycle (0-255) for PWM type
     * @return true if task was added/updated
     */
    static bool addTask(const String& name, const String& type, int pin,
                        unsigned long interval_s, unsigned long duration_s = 0,
                        const String& source = "CMD", bool enabled = true,
                        int value = 0);

    /**
     * @brief Remove a named task
     * @return true if found and removed
     */
    static bool removeTask(const String& name);

    /**
     * @brief Remove all dynamic tasks
     */
    static void clearTasks();

    /**
     * @brief Enable a named task
     */
    static bool enableTask(const String& name);

    /**
     * @brief Disable a named task (pause without removing)
     */
    static bool disableTask(const String& name);

    // ========================================================================
    // Reporting
    // ========================================================================

    /**
     * @brief Build human-readable task list for serial/BLE output
     */
    static String getReport();

    /**
     * @brief Serialize task pool to JSON document (for /api/status)
     */
    static void getJson(JsonDocument& doc);

    /**
     * @brief Return count of active dynamic tasks
     */
    static int getTaskCount();

    // ========================================================================
    // Internal (public for TaskScheduler static callback access)
    // ========================================================================

    static void dynamicTaskCallback();

private:
    // Scheduler state lives entirely in schedule_manager.cpp (file-static)
    // to avoid TaskScheduler.h ODR violations across translation units.
    static std::vector<DynamicTaskConfig> s_configs;

    static void   setupPin(int pin, const String& type, int value);
    static String uptimeStr();
};
