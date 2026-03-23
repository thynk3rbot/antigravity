/**
 * @file hal_compat.h
 * @brief Environment-agnostic hardware compatibility layer
 *
 * Provides mocks for Arduino/ESP32 specifics when building for PC (native).
 */

#pragma once

#ifndef NATIVE_TEST
#include <Arduino.h>
#include "../HAL/mcp_manager.h"

// --- Universal Pin Routing (V1 Parity) ---
// Use these dedicated wrappers instead of native functions to support MCP23017 pins (100-227)
#define uDigitalWrite(p, v) MCPManager::writePin(p, v)
#define uDigitalRead(p)     MCPManager::readPin(p)
#define uPinMode(p, m)      MCPManager::setupPin(p, m)

#else
#include <stdio.h>
#include <chrono>
#include <thread>
#include <string>
#include <stdarg.h>


// --- Standard Arduino Constants ---
#ifndef INPUT
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define HIGH 0x1
#define LOW 0x0
#endif

// --- Mock Serial ---
class MockSerial {
public:
    void begin(unsigned long baud) {}
    void print(const char* s) { printf("%s", s); fflush(stdout); }
    void println(const char* s) { printf("%s\n", s); fflush(stdout); }
    void write(const char* s) { printf("%s", s); fflush(stdout); }
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        fflush(stdout);
    }

    // New stdin support for simulation
    int available() {
        // Simple non-blocking check would be better, but for now we rely on external stimuli
        return 0; 
    }
    int read() { return -1; }

    operator bool() { return true; }
};

extern MockSerial Serial;

// --- Time Functions ---
inline uint32_t millis() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// --- ESP32 Specifics ---
inline uint32_t esp_get_free_heap_size() { return 1024 * 1024; } // Mock 1MB
#endif
