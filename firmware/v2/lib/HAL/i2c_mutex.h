#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Global I2C Mutex to prevent bus contention on Heltec S3 boards
 * 
 * Shared between OLED, MCP23017, and Sensor HAL.
 */
extern SemaphoreHandle_t g_i2cMutex;

#define I2C_LOCK()   if(g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY)
#define I2C_UNLOCK() if(g_i2cMutex) xSemaphoreGive(g_i2cMutex)
