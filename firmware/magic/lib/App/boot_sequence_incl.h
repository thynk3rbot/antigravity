/**
 * @file boot_sequence.cpp
 * @brief Implementation of device startup logic
 */
 
#include "boot_sequence.h"
#include <Arduino.h>
#include "nvs_manager.h"
#include <string>
#include <cstdio>
#include <WiFi.h>
#include <LittleFS.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// HAL Layer
#include "../HAL/board_config.h"
#include "../HAL/mcp_manager.h"
#include "../HAL/relay_hal.h"
#include "../HAL/probe_manager.h"
#include "../HAL/sensor_hal.h"
#include "../HAL/radio_hal.h"
#include "../HAL/i2c_mutex.h"

// Transport Layer
#include "../Transport/message_router.h"
#include "../Transport/lora_transport.h"
#include "../Transport/wifi_transport.h"
#include "../Transport/ble_transport.h"
#include "../Transport/mqtt_transport.h"
#include "../Transport/serial_transport.h"
#include "../Transport/espnow_transport.h"

// Application Layer
#include "power_manager.h"
#include "oled_manager.h"
#include "http_api.h"
#include "command_manager.h"
#include "schedule_manager.h"
#include "gps_manager.h"
#include "product_manager.h"
#include "plugin_manager.h"
#include "message_handler.h"
#include "mesh_coordinator.h"
#include "msg_manager.h"
#include "control_loop.h"
