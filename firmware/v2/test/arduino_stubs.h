/**
 * @file arduino_stubs.h
 * @brief Shared Arduino / ESP32 stub functions for native unit tests
 *
 * Provides lightweight stubs for Arduino/ESP32-specific APIs used by
 * lib/App modules compiled on desktop. Each test file should include
 * this header before any other includes.
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdarg>

// ============================================================================
// Time Mocking
// ============================================================================

/// Global mock time value (in milliseconds)
static uint32_t _mock_millis_value = 0;

/// Return current time (mocked for testing)
inline uint32_t millis() { return _mock_millis_value; }

// ============================================================================
// Serial / Debug Output Stubs
// ============================================================================

#include <string>

/// Minimal String stub for native tests
class String : public std::string {
public:
    String() : std::string("") {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(float f, int p = 2) : std::string(std::to_string(f)) {}
    String(int i) : std::string(std::to_string(i)) {}
    String(unsigned int i) : std::string(std::to_string(i)) {}
    String(long i) : std::string(std::to_string(i)) {}
    String(unsigned long i) : std::string(std::to_string(i)) {}
    
    void replace(const String& find, const String& replace) {
        size_t pos = 0;
        while ((pos = this->find(find, pos)) != std::string::npos) {
            this->std::string::replace(pos, find.length(), replace);
            pos += replace.length();
        }
    }
    
    int toInt() const { return std::stoi(*this); }
    float toFloat() const { return std::stof(*this); }
    
    String substring(int from, int to = -1) const {
        if (to == -1) return String(this->substr(from).c_str());
        return String(this->substr(from, to - from).c_str());
    }

    int indexOf(char c, int from = 0) const {
        size_t pos = this->find(c, from);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    const char* c_str() const { return std::string::c_str(); }
};

/// Minimal IPAddress stub
class IPAddress {
public:
    IPAddress() {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {}
    bool fromString(const String& s) { return true; }
    String toString() const { return "0.0.0.0"; }
};

/// Minimal ESP stub
struct _ESPStub {
    void restart() { printf("[MOCK] ESP.restart() called\n"); }
} ESP;

/// Minimal Serial stub — discards all output
struct _SerialStub {
  /// Discard println
  void println(const char* s) { (void)s; }
  void println(const String& s) { (void)s; }
  void print(const char* s) { (void)s; }

  /// Discard printf (variadic)
  void printf(const char* fmt, ...) {
    (void)fmt;  // Suppress unused warning
  }
} Serial;

#define OUTPUT 0
#define INPUT 1
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline void delay(int) {}

#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(int) {}

inline void esp_sleep_enable_timer_wakeup(uint64_t) {}
inline void esp_deep_sleep_start() {}

// ============================================================================
// Arduino Header Guard
// ============================================================================

/// Prevent mesh_coordinator.cpp from including <Arduino.h>
#define ARDUINO_H

// Note: Helper functions like resetMeshCoordinator() are defined after
// mesh_coordinator.cpp is included in the test files, so MeshCoordinator is
// already available.
