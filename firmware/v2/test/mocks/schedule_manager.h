#pragma once
#include <string>
#include <vector>
#include "../arduino_stubs.h"

class ScheduleManager {
public:
    static String getReport() { return "{}"; }
    static void saveSchedules() {}
    static void clearTasks() {}
    static bool addTask(const String&, const String&, int, unsigned long, unsigned long, const String&) { return true; }
    static bool removeTask(const String&) { return true; }
    static bool enableTask(const String&) { return true; }
    static bool disableTask(const String&) { return true; }
};
