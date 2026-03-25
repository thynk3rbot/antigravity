#pragma once
#include "../arduino_stubs.h"

class MCPManager {
public:
    static bool isMcpPin(int) { return false; }
    static void setupPin(int, int) {}
    static void writePin(int, bool) {}
    static bool readPin(int) { return false; }
};
