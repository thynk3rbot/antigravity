#ifndef DEBUG_MACROS_H
#define DEBUG_MACROS_H

#include <Arduino.h>

// Uncomment to enable debug logging
#define DEBUG_MODE

#ifdef DEBUG_MODE
// SAFETY: Non-blocking prints for ESP32-S3 Native USB
// Prevents the board from hanging if the USB buffer is full or host isn't
// listening.
#define LOG_PRINT(...) if(Serial.availableForWrite() > 32) Serial.print(__VA_ARGS__)
#define LOG_PRINTLN(...) if(Serial.availableForWrite() > 32) Serial.println(__VA_ARGS__)
#define LOG_PRINTF(...) if(Serial.availableForWrite() > 32) Serial.printf(__VA_ARGS__)
#else
#define LOG_PRINT(...)
#define LOG_PRINTLN(...)
#define LOG_PRINTF(...)
#endif

#endif // DEBUG_MACROS_H
