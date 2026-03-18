/**
 * @file main.cpp
 * @brief LoRaLink v2 Main Sketch (Arduino Entry Point)
 *
 * FreeRTOS-based architecture with task separation:
 * - Radio RX task (high priority) - listens for incoming LoRa packets
 * - Control task (normal priority) - executes relay commands, collects telemetry
 * - Main loop - idle (FreeRTOS manages everything)
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// HAL Layer
#include "../lib/HAL/board_config.h"
#include "../lib/HAL/radio_hal.h"
#include "../lib/HAL/relay_hal.h"

// Transport Layer
#include "../lib/Transport/message_router.h"
#include "../lib/Transport/lora_transport.h"
#include "../lib/Transport/interface.h"

// Application Layer
#include "../lib/App/control_packet.h"
#include "../lib/App/mesh_coordinator.h"
#include "../lib/App/nvs_manager.h"

// Bench Mode (optional, compile-time selectable)
#ifdef BENCH_MODE
  #include "../lib/App/bench_test.h"
#endif

// ============================================================================
// Task Handles
// ============================================================================

TaskHandle_t radioTaskHandle = nullptr;
TaskHandle_t controlTaskHandle = nullptr;

// ============================================================================
// Global State
// ============================================================================

uint8_t g_ourNodeID = 0;        // 0 = Hub, 1-254 = Node
uint32_t g_bootTimestamp = 0;

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
    // Poll message router (processes all registered transports)
    // In future: may add MQTT, BLE, Serial transports here
    messageRouter.process();

    // Also poll LoRa directly for any remaining packets
    int len = loraTransport.recv(rxBuffer, sizeof(rxBuffer));  // Non-blocking
    if (len > 0 && len >= (int)sizeof(ControlPacket)) {
      ControlPacket* pkt = (ControlPacket*)rxBuffer;

      // Log packet reception
      Serial.printf("[RX] Type=0x%02X Src=%u Dest=%u RSSI=%d\n",
                    pkt->header.type, pkt->header.src, pkt->header.dest,
                    loraTransport.getSignalStrength());

      // Update mesh topology (neighbor tracking)
      meshCoordinator.updateNeighbor(pkt->header.src,
                                      loraTransport.getSignalStrength(),
                                      1);  // Direct rx = 1 hop

      // Determine relay or consume
      if (pkt->header.dest == g_ourNodeID || pkt->header.dest == 0xFF) {
        // Packet is for us or broadcast - handled by main app logic
        // (messageRouter will call registered handler)
      } else if (meshCoordinator.shouldRelay(*pkt)) {
        // Multi-hop relay needed
        // TODO: Implement relay forwarding with sequence tracking
        Serial.printf("[RELAY] Forwarding to node %u via %u\n",
                      pkt->header.dest, meshCoordinator.getNextHop(pkt->header.dest));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // 100ms poll cycle
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
void controlTask(void* param) {
  static uint32_t lastTelemetrySend = 0;
  static const uint32_t SEND_TELEMETRY_INTERVAL_MS = 10000;  // Every 10 seconds

  while (1) {
    uint32_t now = millis();

    // Collect telemetry
    uint16_t tempC_x10 = 2500;          // Placeholder: 25.0°C
    uint16_t voltageV_x100 = 3300;      // Placeholder: 3.30V
    uint8_t relayState = relayHAL.getState();
    uint8_t rssi = static_cast<uint8_t>(loraTransport.getSignalStrength());

    // Periodically send telemetry
    if (now - lastTelemetrySend >= SEND_TELEMETRY_INTERVAL_MS) {
      ControlPacket telemetry = ControlPacket::makeTelemetry(
        g_ourNodeID,
        0xFF,              // Broadcast to all (Hub will listen)
        tempC_x10,
        voltageV_x100,
        relayState,
        rssi
      );

      int bytesSent = messageRouter.broadcastPacket(
        (uint8_t*)&telemetry,
        sizeof(telemetry)
      );

      Serial.printf("[TX] Telemetry: %d bytes, Temp=%.1f°C, V=%.2fV\n",
                    bytesSent, tempC_x10 / 10.0f, voltageV_x100 / 100.0f);

      lastTelemetrySend = now;
    }

    // Age out stale mesh neighbors
    static uint32_t lastAgeOut = 0;
    if (now - lastAgeOut > 60000) {  // Every 60 seconds
      meshCoordinator.ageOutNeighbors();
      lastAgeOut = now;
    }

    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms control loop period
  }
}

// ============================================================================
// Message Handler Callback (called by MessageRouter)
// ============================================================================

void onMessageReceived(TransportType transportType,
                       const uint8_t* payload, size_t len) {
  if (len < sizeof(ControlPacket)) {
    return;  // Runt packet
  }

  ControlPacket* pkt = (ControlPacket*)payload;

  switch (static_cast<PacketType>(pkt->header.type)) {
    case PacketType::ACTION: {
      // Relay control command
      if (pkt->header.dest == g_ourNodeID || pkt->header.dest == 0xFF) {
        Serial.printf("[ACTION] Toggle relays: mask=0x%02X, state=%d\n",
                      pkt->payload.action.relayMask,
                      pkt->payload.action.relayState);

        // Apply relay state changes
        uint8_t currentState = relayHAL.getState();
        uint8_t newState = currentState;

        // Toggle affected channels
        for (int i = 0; i < 8; i++) {
          if (pkt->payload.action.relayMask & (1 << i)) {
            if (pkt->payload.action.relayState) {
              newState |= (1 << i);   // Turn ON
            } else {
              newState &= ~(1 << i);  // Turn OFF
            }
          }
        }

        relayHAL.setState(newState);

        // Send ACK if requested
        if (pkt->header.requiresACK()) {
          ControlPacket ack = ControlPacket::makeACK(
            g_ourNodeID, pkt->header.src, pkt->header.seq
          );
          messageRouter.broadcastPacket((uint8_t*)&ack, sizeof(ack));
          Serial.printf("[ACK] Sent for seq=%u\n", pkt->header.seq);
        }
      }
      break;
    }

    case PacketType::TELEMETRY: {
      // Telemetry from remote node (usually consumed by Hub/server)
      if (g_ourNodeID == 0) {  // If we're the Hub
        Serial.printf("[TELEMETRY] Node %u: Temp=%.1f°C, V=%.2fV, Relays=0x%02X\n",
                      pkt->header.src,
                      pkt->payload.telemetry.tempC_x10 / 10.0f,
                      pkt->payload.telemetry.voltageV_x100 / 100.0f,
                      pkt->payload.telemetry.relayState);
      }
      break;
    }

    case PacketType::ACK: {
      Serial.printf("[ACK_RX] From node %u, seq=%u\n",
                    pkt->header.src, pkt->header.seq);
      break;
    }

    case PacketType::HEARTBEAT: {
      Serial.printf("[HEARTBEAT] From node %u\n", pkt->header.src);
      break;
    }

    default:
      Serial.printf("[UNKNOWN] Packet type 0x%02X\n", pkt->header.type);
      break;
  }
}

// ============================================================================
// Setup (Arduino Entry Point)
// ============================================================================

void setup() {
  // Initialize serial early for debug output
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println("\n\n=== LoRaLink v2 Boot ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Role: %s\n", DEVICE_ROLE);
  Serial.printf("Radio: %s\n", RADIO_MODEL);
  Serial.printf("PSRAM: %s\n", HAS_PSRAM_STR);

  // Record boot timestamp
  g_bootTimestamp = millis();

  // ========================================================================
  // Step 1: Initialize NVS (Persistent Storage)
  // ========================================================================
  Serial.println("\n[1/7] Initializing NVS persistence layer...");

  if (!NVSManager::init()) {
    Serial.println("WARNING: NVS init failed - using defaults");
    // Continue anyway, but config won't persist across reboots
  }

  NVSManager::printInfo();  // Debug output

  // ========================================================================
  // Step 2: Initialize HAL (GPIO, SPI, etc.)
  // ========================================================================
  Serial.println("[2/7] Initializing HAL...");

  if (!radioHAL.init()) {
    Serial.println("ERROR: Radio HAL init failed!");
    while (1) delay(1000);
  }

  relayHAL.init();
  Serial.println("  ✓ HAL initialized");

  // ========================================================================
  // Step 3: Initialize Transport Layer
  // ========================================================================
  Serial.println("[3/7] Initializing transports...");

  if (!loraTransport.init()) {
    Serial.println("ERROR: LoRa transport init failed!");
    while (1) delay(1000);
  }

  messageRouter.registerTransport(&loraTransport);
  messageRouter.setMessageHandler(onMessageReceived);
  Serial.println("  ✓ Transport initialized");

  // ========================================================================
  // Step 4: Initialize Application Layer
  // ========================================================================
  Serial.println("[4/7] Initializing application...");

  #ifdef ROLE_HUB
    g_ourNodeID = 0;
    Serial.println("  ✓ Hub mode");
  #else
    g_ourNodeID = 1;  // TODO: Read from NVS or config
    Serial.println("  ✓ Node mode");
  #endif

  meshCoordinator.init();
  meshCoordinator.setOwnNodeID(g_ourNodeID);
  Serial.println("  ✓ Mesh coordinator initialized");

  // Safe stagger delay (prevent brownout during boot)
  delay(BOOT_SAFE_DELAY_STAGGER_MS);

  // ========================================================================
  // Step 4.5 (Optional): Run Bench Mode Diagnostics
  // ========================================================================
  #ifdef BENCH_MODE
    Serial.println("[4.5/7] Running bench mode diagnostics...");
    bool benchPassed = BenchTest::runAll();
    if (!benchPassed) {
      Serial.println("\n⚠️  WARNING: Some bench tests failed!");
      Serial.println("    Review output above for details.");
      delay(2000);
    }
    // Reinitialize hardware after bench tests (they may stress hardware)
    Serial.println("[4.5/7] Re-initializing hardware after bench mode...");
    radioHAL.init();
    relayHAL.init();
    loraTransport.init();
    Serial.println("  ✓ Hardware re-initialized");
  #endif

  // ========================================================================
  // Step 5: Create FreeRTOS Tasks
  // ========================================================================
  Serial.println("[5/7] Creating FreeRTOS tasks...");

  xTaskCreatePinnedToCore(
    radioTask,                           // Task function
    "RadioRx",                           // Task name
    4096,                                // Stack size (bytes)
    nullptr,                             // Parameter
    3,                                   // Priority (0-24, higher = more urgent)
    &radioTaskHandle,
    0                                    // Core (0 or 1)
  );

  xTaskCreatePinnedToCore(
    controlTask,
    "Control",
    4096,
    nullptr,
    2,                                   // Lower priority than radio
    &controlTaskHandle,
    1                                    // Run on core 1 to avoid contention
  );

  Serial.println("  ✓ Tasks created");

  // ========================================================================
  // Step 6: Boot Complete
  // ========================================================================
  Serial.println("[6/7] Boot complete!");
  Serial.printf("  Uptime: %lu ms\n", millis() - g_bootTimestamp);
  Serial.println("\n[7/7] Entering main loop (FreeRTOS)...");
  Serial.println("===========================\n");
}

// ============================================================================
// Main Loop (Arduino Loop)
// ============================================================================
/**
 * @brief Main idle loop
 * FreeRTOS manages all task scheduling.
 * This function is called by the idle task and should yield frequently.
 */
void loop() {
  // FreeRTOS tasks handle everything
  // Sleep to give other tasks CPU time
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Periodic status output (every ~10 seconds)
  static uint32_t lastStatus = 0;
  uint32_t now = millis();

  if (now - lastStatus > 10000) {
    Serial.printf("[STATUS] Uptime: %lu s, Neighbors: %u, Relayed: %u\n",
                  (now - g_bootTimestamp) / 1000,
                  meshCoordinator.getNeighborCount(),
                  meshCoordinator.getRelayCount());

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
