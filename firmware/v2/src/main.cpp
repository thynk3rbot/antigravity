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
#include <esp_heap_caps.h>

// HAL Layer
#include "../lib/HAL/board_config.h"
#include "../lib/HAL/radio_hal.h"
#include "../lib/HAL/relay_hal.h"
#include "../lib/HAL/sensor_hal.h"
#include "../lib/HAL/dht_sensor.h"

// Transport Layer
#include "../lib/Transport/message_router.h"
#include "../lib/Transport/lora_transport.h"
#include "../lib/Transport/wifi_transport.h"
#include "../lib/Transport/ble_transport.h"
#include "../lib/Transport/mqtt_transport.h"
#include "../lib/Transport/serial_transport.h"
#include "../lib/Transport/interface.h"

// Application Layer
#include "../lib/App/control_packet.h"
#include "../lib/App/mesh_coordinator.h"
#include "../lib/App/nvs_manager.h"
#include "../lib/App/nvs_config.h"
#include "../lib/App/power_manager.h"
#include "../lib/App/oled_manager.h"
#include "../lib/App/http_api.h"
#include "../lib/App/command_manager.h"
#include "../lib/App/schedule_manager.h"
#include "../lib/App/gps_manager.h"

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
    messageRouter.process();

    // Poll human-readable CLI transports (Serial and BLE)
    serialTransport.poll();
    BLETransport::pollStatic();

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
        pkt->header.flags |= PKT_FLAG_IS_RELAY; // Mark as relayed
        
        // Forward via MessageRouter (will pick best transport or broadcast)
        messageRouter.broadcastPacket((uint8_t*)pkt, sizeof(ControlPacket));
        
        Serial.printf("[RELAY] Forwarded packet from %u to %u\n",
                      pkt->header.src, pkt->header.dest);
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
  static uint32_t lastPowerUpdate = 0;
  static const uint32_t POWER_UPDATE_INTERVAL_MS = 30000;  // Every 30 seconds
  static uint32_t lastOLEDUpdate = 0;
  static const uint32_t OLED_UPDATE_INTERVAL_MS = 500;    // Every 500ms

  while (1) {
    uint32_t now = millis();

    // =====================================================================
    // Update Power Manager (battery monitoring)
    // =====================================================================
    if (now - lastPowerUpdate >= POWER_UPDATE_INTERVAL_MS) {
      PowerManager::update();
      lastPowerUpdate = now;
    }

    // Collect telemetry
    uint16_t tempC_x10 = 2500;          // Placeholder: 25.0°C
    uint16_t voltageV_x100 = static_cast<uint16_t>(PowerManager::getBatteryVoltage() * 100.0f);
    uint8_t relayState = relayHAL.getState();
    int8_t rssi = loraTransport.getSignalStrength();

    // =====================================================================
    // Update OLED Display Cache & Refresh
    // =====================================================================
    if (now - lastOLEDUpdate >= OLED_UPDATE_INTERVAL_MS) {
      // Update cached values for OLED display
      static const char* const kModeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};
      OLEDManager::setBatteryVoltage(PowerManager::getBatteryVoltage(),
                                     kModeNames[static_cast<uint8_t>(PowerManager::getMode())]);
      OLEDManager::setLoRaSignal(rssi, 0);  // TODO: add SNR from LoRa
      OLEDManager::setRelayStatus(relayState > 0);
      OLEDManager::setTemperature(tempC_x10 / 10.0f);
      OLEDManager::setPeerCount(meshCoordinator.getNeighborCount());
      OLEDManager::setUptime(now - g_bootTimestamp);
      OLEDManager::setFreeHeap(esp_get_free_heap_size());

      GPSManager::GPSData gpsData = GPSManager::getData();
      OLEDManager::setGPS(gpsData.lat, gpsData.lon, gpsData.satellites, gpsData.hasFix);
      // Refresh display
      OLEDManager::update();
      lastOLEDUpdate = now;

      // =====================================================================
      // Update CommandManager status for CLI/BLE
      // =====================================================================
      CommandManager::StatusData status;
      status.nodeId = NVSManager::getNodeID("Node").c_str();
      status.version = FIRMWARE_VERSION;
      status.ipAddr = WiFi.localIP().toString();
      status.batVoltage = PowerManager::getBatteryVoltage();
      status.batPercent = PowerManager::getBatteryPercent();
      status.powerMode = kModeNames[static_cast<uint8_t>(PowerManager::getMode())];
      status.relay1 = (relayHAL.getState() & 0x01);
      status.relay2 = (relayHAL.getState() & 0x02);
      status.loraRSSI = rssi;
      status.loraSNR = loraTransport.getLastSNR();
      status.loraTX = loraTransport.getTxBytes();
      status.loraRX = loraTransport.getRxBytes();
      status.meshNeighbors = meshCoordinator.getNeighborCount();
      status.uptime = now - g_bootTimestamp;
      status.freeHeap = esp_get_free_heap_size();

      CommandManager::updateStatus(status);
      
      // Heartbeat status print to serial every 10 iterations (5 seconds)
      static uint8_t iterations = 0;
      if (++iterations >= 10) {
          iterations = 0;
          Serial.printf("[HEARTBEAT] Bat: %.2fV, RSSI: %d, Neighbors: %u, Heap: %u\n", 
                        status.batVoltage, status.loraRSSI, status.meshNeighbors, status.freeHeap);
      }
    }

    // Periodically send telemetry
    if (now - lastTelemetrySend >= SEND_TELEMETRY_INTERVAL_MS) {
      ControlPacket telemetry = ControlPacket::makeTelemetry(
        g_ourNodeID,
        0xFF,              // Broadcast to all (Hub will listen)
        tempC_x10,
        voltageV_x100,
        relayState,
        static_cast<uint8_t>(rssi)
      );

      int bytesSent = messageRouter.broadcastPacket(
        (uint8_t*)&telemetry,
        sizeof(telemetry)
      );

      Serial.printf("[TX] Telemetry: %d bytes, Temp=%.1f°C, V=%.2fV\n",
                    bytesSent, tempC_x10 / 10.0f, voltageV_x100 / 100.0f);

      lastTelemetrySend = now;
    }

    // Advance scheduled GPIO tasks (TaskScheduler cooperative poll)
    ScheduleManager::execute();

    // Age out stale mesh neighbors
    static uint32_t lastAgeOut = 0;
    if (now - lastAgeOut > 60000) {  // Every 60 seconds
      meshCoordinator.ageOutNeighbors();
      lastAgeOut = now;
    }

    // Run periodic mesh activities (Discovery, Heartbeat)
    meshCoordinator.poll();

    // Poll MQTT transport (handles connection, message receipt, telemetry publish)
    #ifdef ENABLE_MQTT_TRANSPORT
      MQTTTransport::pollStatic();
    #endif

    // Update GNSS tracking
    GPSManager::update();

    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms control loop period
  }
}

// ============================================================================
// Message Handler Callback (called by MessageRouter)
// ============================================================================

void onMessageReceived(TransportType transportType,
                       const uint8_t* payload, size_t len) {
  if (len < sizeof(ControlPacket)) {
    // Check if it's a V1 legacy packet
    if (meshCoordinator.handleV1Packet(payload, len)) {
        return; // Handled as V1
    }
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
  // 1. Initialize serial IMMEDIATELY
  Serial.begin(SERIAL_BAUD);
  
  // Wait for Native USB Serial to connect (max 3 seconds)
  uint32_t startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  
  delay(500);
  Serial.println("\n\n=== LoRaLink v2 Boot ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Role: %s\n", DEVICE_ROLE);
  Serial.printf("Radio: %s\n", RADIO_MODEL);
  Serial.printf("PSRAM: %s\n", HAS_PSRAM_STR);

  // Record boot timestamp
  g_bootTimestamp = millis();

  Serial.println("\n\n=== LoRaLink v2 Boot ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Role: %s\n", DEVICE_ROLE);
  Serial.printf("Radio: %s\n", RADIO_MODEL);
  Serial.printf("PSRAM: %s\n", HAS_PSRAM_STR);

  // Step 1: Initialize NVS (Persistent Storage)
  // ========================================================================
  Serial.println("\n[1/7] Initializing NVS persistence layer...");

  if (!NVSManager::init()) {
    Serial.println("WARNING: NVS init failed - using defaults");
    // Continue anyway, but config won't persist across reboots
  }

  // Initialize NVSConfig for boot diagnostics and Arduino-style prefs
  NVSConfig::begin();

  NVSManager::printInfo();  // Debug output

  // Extract MAC suffix for OLED identification
  uint8_t mac_raw[6];
  esp_efuse_mac_get_default(mac_raw);
  char mac_buf[8];
  snprintf(mac_buf, sizeof(mac_buf), "[%02X:%02X]", mac_raw[4], mac_raw[5]);
  OLEDManager::setMAC(mac_buf);

  // Set diagnostics in OLED
  OLEDManager::setDiagnostics(NVSConfig::getBootCount(), NVSConfig::getResetReason().c_str());
  OLEDManager::addLog("System Initializing");

  // ========================================================================
  // Step 2: Initialize Power Manager (Battery Voltage Monitoring)
  // ========================================================================
  Serial.println("[2/7] Initializing power manager...");

  if (!PowerManager::init()) {
    Serial.println("WARNING: Power manager init failed");
    // Continue anyway, but battery monitoring will not work
  }

  PowerManager::onModeChange([](PowerMode mode) {
    const char* modeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};
    Serial.printf("[PowerMgr] Mode change: %s\n", modeNames[static_cast<uint8_t>(mode)]);
    // Update OLED display when power mode changes
    OLEDManager::update();
  });

  // Enable external power rail (VEXT) — powers OLED and peripherals on Heltec boards.
  // Must happen before Wire.begin() / OLED init or the display has no power.
  PowerManager::enableVEXT();
  delay(50);  // Allow rail to stabilize

  Serial.println("  ✓ Power manager initialized");

  // ========================================================================
  // Step 2.5: Initialize OLED Display
  // ========================================================================
  Serial.println("[2.5/7] Initializing OLED display...");

  if (!OLEDManager::init()) {
    Serial.println("WARNING: OLED init failed (non-fatal)");
    // Continue anyway, device will work without display
  } else {
    Serial.println("  ✓ OLED display initialized");
  }

  delay(BOOT_SAFE_DELAY_STAGGER_MS);

  // ========================================================================
  // Step 3: Initialize HAL (GPIO, SPI, etc.)
  // ========================================================================
  Serial.println("[3/8] Initializing HAL...");

  if (!radioHAL.init()) {
    Serial.println("ERROR: Radio HAL init failed!");
    while (1) delay(1000);
  }

  relayHAL.init();
  
  // Initialize Sensors
  #ifdef PIN_SENSOR_DHT
    static DHTSensorPlugin dhtPlugin(PIN_SENSOR_DHT);
    sensorHAL.registerPlugin(&dhtPlugin);
  #endif
  sensorHAL.init();

  Serial.println("  ✓ HAL initialized");

  // Get Node ID early for diagnostics and BLE
  std::string nodeIDStr = NVSManager::getNodeID("Node");
  if (nodeIDStr.empty()) nodeIDStr = "Unknown";

  // LoRa Transport
  if (!loraTransport.init()) {
    Serial.println("  ! LoRa transport init failed (check hardware)");
  } else {
    messageRouter.registerTransport(&loraTransport);
    Serial.println("  ✓ LoRa transport initialized");
  }

  // Serial CLI Transport (Always active on Native USB)
  if (!serialTransport.init()) {
      Serial.println("  ! Serial CLI init failed");
  } else {
      Serial.println("  ✓ Serial CLI initialized");
      OLEDManager::addLog("Serial CLI Ready");
  }

  // BLE Transport (Always active)
  if (!BLETransport::initStatic()) {
      Serial.println("  ! BLE transport failed to start");
  } else {
      Serial.println("  ✓ BLE transport initialized");
  }

  // Optional WiFi Transport (graceful fallback if connection fails)
  std::string wifiSSID = NVSManager::getWiFiSSID();
  std::string wifiPass = NVSManager::getWiFiPassword();

  // Build mDNS hostname from node ID
  std::string mdnsHostname = "loralink-" + nodeIDStr;

  if (!wifiSSID.empty() && !wifiPass.empty()) {
    if (WiFiTransport::init(wifiSSID, wifiPass, mdnsHostname)) {
      Serial.println("  ✓ WiFi transport initialized (connecting...)");

      // Initialize HTTP API server
      if (HttpAPI::init()) {
        Serial.println("  ✓ HTTP API server started on port 80");
      } else {
        Serial.println("  ! HTTP API server failed to start (non-fatal)");
      }
    } else {
      Serial.println("  ! WiFi transport init failed, continuing without WiFi");
    }
  } else {
    Serial.println("  ! WiFi credentials not configured, skipping WiFi");
  }

  // Initialize Command Manager
  CommandManager::begin();
  CommandManager::setRelayCallback([](uint8_t relay, bool state) {
    // relay is 1-based; setState uses a bitmask over all channels
    uint8_t mask = relayHAL.getState();
    uint8_t bit  = relay - 1;
    if (state) mask |=  (1u << bit);
    else       mask &= ~(1u << bit);
    relayHAL.setState(mask);
  });

  // Optional BLE Transport — RX wired to CommandManager
  if (!BLETransport::initStatic()) {
    Serial.println("  ! BLE transport init failed (non-fatal)");
  } else {
    Serial.printf("  ✓ BLE transport initialized (%s)\n", nodeIDStr.c_str());
    BLETransport::setRxCallback([](const String& data) {
      Serial.printf("[BLE] Command received: '%s'\n", data.c_str());
      CommandManager::process(data, [](const String& resp) {
        BLETransport::sendStringStatic(resp);
        Serial.printf("[BLE] Response sent: %u bytes\n", resp.length());
      });
    });
    OLEDManager::addLog("BLE Ready");
  }

  // Optional MQTT Transport (Hub only, requires WiFi and broker configured)
  #ifdef ENABLE_MQTT_TRANSPORT
    if (MQTTTransport::initStatic()) {
      Serial.println("  ✓ MQTT transport initialized (configuring broker...)");
      MQTTTransport::onCommand([](const std::string& cmd) {
        CommandManager::process(String(cmd.c_str()), [](const String& resp) {
          MQTTTransport::instance()->publishResponse(resp);
        });
      });
    } else {
      Serial.println("  ! MQTT transport init failed (non-fatal)");
    }
  #endif

  // ========================================================================
  // Step 5: Initialize Application Layer
  // ========================================================================
  Serial.println("[5/8] Initializing application...");

  #ifdef ROLE_HUB
    g_ourNodeID = 0;
    Serial.println("  ✓ Hub mode");
  #else
    g_ourNodeID = 1;  // TODO: Read from NVS or config
    Serial.println("  ✓ Node mode");
  #endif

  // Initialize GNSS (V4 support)
  GPSManager::init();

  meshCoordinator.init();
  meshCoordinator.setOwnNodeID(g_ourNodeID);
  Serial.println("  ✓ Mesh coordinator initialized");

  // Initialize GPIO scheduler (loads /schedule.json from LittleFS)
  if (ScheduleManager::init()) {
    Serial.printf("  ✓ Scheduler initialized (%d task(s) loaded)\n",
                  ScheduleManager::getTaskCount());
  } else {
    Serial.println("  ! Scheduler init failed (non-fatal)");
  }

  // ========================================================================
  // Step 6 (Optional): Run Bench Mode Diagnostics
  // ========================================================================
  #ifdef BENCH_MODE
    Serial.println("[6/8] Running bench mode diagnostics...");
    bool benchPassed = BenchTest::runAll();
    if (!benchPassed) {
      Serial.println("\n⚠️  WARNING: Some bench tests failed!");
      Serial.println("    Review output above for details.");
      delay(2000);
    }
    // Reinitialize hardware after bench tests (they may stress hardware)
    Serial.println("[6/8] Re-initializing hardware after bench mode...");
    radioHAL.init();
    relayHAL.init();
    loraTransport.init();
    Serial.println("  ✓ Hardware re-initialized");
  #endif

  // ========================================================================
  // Step 7: Create FreeRTOS Tasks
  // ========================================================================
  Serial.println("[7/8] Creating FreeRTOS tasks...");

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
  // Step 8: Boot Complete
  // ========================================================================
  Serial.println("[8/8] Boot complete!");
  Serial.printf("  Uptime: %lu ms\n", millis() - g_bootTimestamp);

  // Print diagnostics
  PowerManager::printStatus();
  OLEDManager::printStatus();

  Serial.println("[9/9] Entering main loop (FreeRTOS)...");
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
    Serial.printf("[STATUS] Uptime: %u s, Neighbors: %u, Relayed: %u\n",
                  (unsigned int)((now - g_bootTimestamp) / 1000),
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
