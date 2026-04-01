/**
 * @file main.cpp
 * @brief Magic v2 Main Sketch (Arduino Entry Point)
 *
 * FreeRTOS-based architecture with task separation:
 * - Radio RX task (high priority) - listens for incoming LoRa packets
 * - Control task (normal priority) - executes relay commands, collects telemetry
 * - Main loop - idle (FreeRTOS manages everything)
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <stdio.h>

#include "../lib/App/nvs_manager.h"
#include "../lib/App/boot_sequence.h"
#include "../lib/App/control_loop.h"
#include "../lib/HAL/probe_manager.h"
#include "../lib/Transport/message_router.h"
#include "../lib/Transport/lora_transport.h"
#include "../lib/Transport/serial_transport.h"
#include "../lib/Transport/wifi_transport.h"
#include "../lib/Transport/ble_transport.h"
#include "../lib/Transport/mqtt_transport.h"
#include "../lib/App/mesh_coordinator.h"
#include "../lib/App/control_packet.h"
#include "../lib/App/status_builder.h"
#include "../lib/HAL/relay_hal.h"
#include "../lib/App/oled_manager.h"
#include "../lib/Transport/mqtt_transport.h"

// ============================================================================
// Forward Declarations
// ============================================================================
void radioTask(void* param);
void meshTask(void* param);
void probeTask(void* param);
void displayTask(void* param);
void wifiTask(void* param);

// ============================================================================
// Task Handles
// ============================================================================

TaskHandle_t meshTaskHandle = NULL;
TaskHandle_t radioTaskHandle = NULL;
TaskHandle_t probeTaskHandle = NULL; // New: Marauder Background Sniffing
TaskHandle_t controlTaskHandle = nullptr;

// ============================================================================
// Global State
// ============================================================================

uint8_t g_ourNodeID = 0;        // 0 = Hub, 1-254 = Node
uint32_t g_bootTimestamp = 0;
SemaphoreHandle_t g_i2cMutex = nullptr;

// ============================================================================
// Task 1: Radio Receiver (High Priority)
// ============================================================================
/**
 * @brief Radio RX task
 * Continuously listens for LoRa packets with minimal latency.
 * Non-blocking 100ms polling prevents packet loss.
 *
 * Priority: 3 (high, core 0)
 * Period: 100ms
 */
void radioTask(void* param) {
  uint8_t rxBuffer[256];

  while (1) {
    // Wait for ISR notification (max 100ms for other task service)
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    // Service transport processing
    messageRouter.process();
    serialTransport.poll();
    BLETransport::pollStatic();

    // Check for LoRa packets
    int len = loraTransport.recv(rxBuffer, sizeof(rxBuffer));
    if (len > 0 && len >= (int)sizeof(ControlPacket)) {
      ControlPacket* pkt = (ControlPacket*)rxBuffer;

      // Log packet reception
      Serial.printf("[RX] Type=0x%02X Src=%u Dest=%u RSSI=%d\n",
                    pkt->header.type, pkt->header.src, pkt->header.dest,
                    loraTransport.getSignalStrength());

      // Update mesh topology
      meshCoordinator.updateNeighbor(pkt->header.src,
                                      loraTransport.getSignalStrength(),
                                      1);

      // Consume or Relay
      if (pkt->header.dest == g_ourNodeID || pkt->header.dest == 0xFF) {
        // Handled by router callbacks
      } else if (meshCoordinator.shouldRelay(*pkt)) {
        pkt->header.flags |= PKT_FLAG_IS_RELAY;
        messageRouter.broadcastPacket((uint8_t*)pkt, sizeof(ControlPacket));
        Serial.printf("[RELAY] Forwarded packet from %u to %u\n",
                      pkt->header.src, pkt->header.dest);
      }
    }
  }
}

// ============================================================================
// Task 2: Control Executor (Normal Priority)
// ============================================================================
/**
 * @brief Control & telemetry task
 * Executes relay commands, collects ADC samples, sends periodic telemetry.
 *
 * Priority: 2 (normal, core 1)
 * Period: 500ms
 */
// controlTask logic moved to ControlLoop::execute

void probeTask(void *pvParameters) {
  Serial.println("[Task] Probe/Marauder Background Task Started");
  for (;;) {
    ProbeManager::getInstance().service();
    vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz scanning/hopping pulse
  }
}

/**
 * @brief Mesh Topology Task
 * Handles neighbor aging, discovery, and heartbeat logic.
 */
void meshTask(void *pvParameters) {
  Serial.println("[Task] Mesh State Machine Started");
  for (;;) {
    meshCoordinator.poll();
    vTaskDelay(pdMS_TO_TICKS(500)); // 2Hz topology pulse
  }
}

// ============================================================================
// Message Handler Callback (called by MessageRouter)
// ============================================================================

// onMessageReceived logic moved to MessageHandler::handleReceived

// ============================================================================
// Setup (Arduino Entry Point)
// ============================================================================
void setup() {
  BootSequence::run();
}

// ============================================================================
// Main Loop (Arduino Loop)
// ============================================================================
/**
 * @brief Display Update Task (Low Priority)
 * Handles blocking I2C display operations without starving control loop.
 * The OLEDManager::update() function is called at 100Hz in control loop for button responsiveness,
 * but the actual display.display() call (full I2C buffer send, 50-100ms) is deferred to this task.
 * Priority: 1 (low, core 1)
 * Period: 100ms (10Hz) — limits display updates, sufficient for visual feedback
 */
void displayTask(void* param) {
  Serial.println("[Task] Display Update Task Started");
  for (;;) {
    // Deferred display refresh (once per 100ms = 10Hz)
    // Actual data updates happen in control loop, just the I2C send is deferred
    vTaskDelay(pdMS_TO_TICKS(100));
    OLEDManager::getInstance().deferredRefresh();
  }
}

/**
 * @brief WiFi & MQTT Service Task (Low Priority)
 * Handles blocking WiFi/MQTT operations without starving control loop.
 * Priority: 1 (low, core 1)
 * Period: 50ms (20Hz)
 */
void wifiTask(void* param) {
  Serial.println("[Task] WiFi/MQTT Service Task Started");
  for (;;) {
    // Drive OTA and WiFi reconnects with explicit timeout
    WiFiTransport::service();
#ifdef ENABLE_MQTT_TRANSPORT
    MQTTTransport::pollStatic();
#endif
    // Feed WDT after OTA/mDNS work — prevents async_tcp starvation cascade
    // when WiFi stack locks are briefly held during mDNS advertisement
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Main idle loop (Arduino Entry Point)
 * FreeRTOS manages all task scheduling.
 * This function is called by the idle task and should yield frequently.
 * REMOVED: WiFi service (now in dedicated task)
 */
void loop() {
  // Pure idle loop - WiFi service moved to wifiTask
  // This allows FreeRTOS scheduler to run uninterrupted
  vTaskDelay(pdMS_TO_TICKS(10));

  // Handle Serial Commands (Simple CLI)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      CommandManager::process(input, [](const String& response) {
        Serial.println(response);
      });
    }
  }

  // Periodic status output (every ~10 seconds)
  static uint32_t lastStatus = 0;
  uint32_t now = millis();

  if (now - lastStatus > 10000) {
    // Periodic JSON status output (every ~10 seconds) for Web App / PC serial bridge
    Serial.print("[JSON_STATUS] ");
    Serial.println(StatusBuilder::buildStatusString().c_str());

    lastStatus = now;
  }
}

// ============================================================================
// (Optional) Setup additional transports in future
// ============================================================================

/*
 * Future expansion points:
 *
 * - MQTT Transport (Hub only):
 *   mqttTransport.init();
 *   messageRouter.registerTransport(&mqttTransport);
 *
 * - Serial CLI Transport (debug):
 *   serialTransport.init();
 *   messageRouter.registerTransport(&serialTransport);
 *
 * - BLE Transport (V4 with PSRAM):
 *   #ifdef BOARD_HAS_PSRAM
 *     bleTransport.init();
 *     messageRouter.registerTransport(&bleTransport);
 *   #endif
 */
