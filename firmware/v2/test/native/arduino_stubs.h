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

/// Minimal Serial stub — discards all output
struct _SerialStub {
  /// Discard println
  void println(const char*) {}

  /// Discard printf (variadic)
  void printf(const char* fmt, ...) {
    (void)fmt;  // Suppress unused warning
  }
} Serial;

// ============================================================================
// Arduino Header Guard
// ============================================================================

/// Prevent mesh_coordinator.cpp from including <Arduino.h>
#define ARDUINO_H

// Note: Helper functions like resetMeshCoordinator() are defined after
// mesh_coordinator.cpp is included in the test files, so MeshCoordinator is
// already available.
