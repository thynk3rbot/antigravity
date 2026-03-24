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
#include "../lib/App/plugin_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>

// HAL Layer
#include "../lib/HAL/board_config.h"
#include "../lib/HAL/mcp_manager.h"
#include "../lib/HAL/relay_hal.h"
#include "../lib/HAL/probe_manager.h"
#include "../lib/HAL/sensor_hal.h"
/* NutriCalc decoupled */

// App Logic
#include "../lib/App/product_manager.h"
#ifdef PIN_SENSOR_DHT
#include "../lib/HAL/dht_sensor.h"
#endif

// Transport Layer
#include "../lib/Transport/message_router.h"
#include "../lib/Transport/lora_transport.h"
#include "../lib/Transport/wifi_transport.h"
#include "../lib/Transport/ble_transport.h"
#include "../lib/Transport/mqtt_transport.h"
#include "../lib/Transport/serial_transport.h"
#include "../lib/Transport/interface.h"
#include "../lib/Transport/espnow_transport.h"

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
#include "../lib/App/status_builder.h"

// Bench Mode (optional, compile-time selectable)
#ifdef BENCH_MODE
  #include "../lib/App/bench_test.h"
#endif

// ============================================================================
// Forward Declarations
// ============================================================================
void radioTask(void* param);
void meshTask(void* param);
void probeTask(void* param);

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
    uint16_t tempC_x10 = 2500;  // Default
    auto sensorReadings = sensorHAL.readAll();
    for (const auto& r : sensorReadings) {
        if (r.type == SensorType::TEMPERATURE) {
            tempC_x10 = static_cast<uint16_t>(r.value * 10.0f);
            break;
        }
    }
    uint16_t voltageV_x100 = static_cast<uint16_t>(PowerManager::getBatteryVoltage() * 100.0f);
    uint8_t relayState = relayHAL.getState();
    int8_t rssi = loraTransport.getSignalStrength();

    // =====================================================================
    // Update OLED Display Cache & Refresh
    // =====================================================================
    if (now - lastOLEDUpdate >= OLED_UPDATE_INTERVAL_MS) {
      // Update cached values for OLED display
      static const char* const kModeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};
      OLEDManager::getInstance().setBatteryVoltage(PowerManager::getBatteryVoltage(),
                                     kModeNames[static_cast<uint8_t>(PowerManager::getMode())]);
      OLEDManager::getInstance().setLoRaSignal(rssi, loraTransport.getLastSNR());
      OLEDManager::getInstance().setRelayStatus(relayState > 0);
      OLEDManager::getInstance().setTemperature(tempC_x10 / 10.0f);
      OLEDManager::getInstance().setPeerCount(meshCoordinator.getNeighborCount());
      OLEDManager::getInstance().setUptime(now - g_bootTimestamp);
      OLEDManager::getInstance().setFreeHeap(esp_get_free_heap_size());
      OLEDManager::getInstance().setDeviceName(NVSManager::getNodeID("Node").c_str());
      OLEDManager::getInstance().setVersion(FIRMWARE_VERSION);
      
      OLEDManager::getInstance().setIP(WiFi.localIP().toString().c_str());
      OLEDManager::getInstance().setTransportStatus(
        WiFi.status() == WL_CONNECTED,
        BLETransport::isConnected(),
        #ifdef ENABLE_MQTT_TRANSPORT
          MQTTTransport::instance()->isConnected(),
        #else
          false,
        #endif
        loraTransport.isReady(),
        espNowTransport.isReady()
      );

#ifdef HAS_GPS
      GPSManager::GPSData gpsData = GPSManager::getData();
      OLEDManager::getInstance().setGPS(gpsData.lat, gpsData.lon, gpsData.satellites, gpsData.hasFix);
#endif
      // Refresh display
      OLEDManager::getInstance().update();
      lastOLEDUpdate = now;

      // =====================================================================
      // Update CommandManager status for CLI/BLE
      // =====================================================================
      CommandManager::StatusData status;
      status.nodeId = NVSManager::getNodeID("Node").c_str();
      status.version = FIRMWARE_VERSION;
      status.hw = HW_VERSION;
      status.mac = WiFi.macAddress();
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

    // Age out stale mesh neighbors handled in MeshTask

    // Poll MQTT transport (handles connection, message receipt, telemetry publish)
    #ifdef ENABLE_MQTT_TRANSPORT
      MQTTTransport::pollStatic();
    #endif

#ifdef HAS_GPS
    // Update GNSS tracking
    GPSManager::update();
#endif

    // Poll all generic plugins
    pluginManager.pollAll();

    vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz loop
  }
}

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
      // Received telemetry from another peer
      Serial.printf("[TELEMETRY] From Peer %u: Temp=%.1f°C, V=%.2fV, Relays=0x%02X\n",
                    pkt->header.src,
                    (float)pkt->payload.telemetry.tempC_x10 / 10.0f,
                    (float)pkt->payload.telemetry.voltageV_x100 / 100.0f,
                    pkt->payload.telemetry.relayState);
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
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Record boot timestamp
  g_bootTimestamp = millis();

  // 2. Initialize Power & Rails IMMEDIATELY (V2 requires this for I2C)
  PowerManager::init();
  PowerManager::enableVEXT();
  vTaskDelay(pdMS_TO_TICKS(500)); // Phase 1: Power stabilization window
  
  Serial.println("[CHECK] Initializing I2C...");
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
  Wire.begin(I2C_SDA, I2C_SCL, 100000); // V2 uses 100K for better signal integrity on legacy bus
#else
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
#endif
  
  Serial.println("[CHECK] Initializing LittleFS...");
  if (!LittleFS.begin(true)) {
    Serial.println("WARNING: LittleFS mount failed");
  }

  Serial.println("[CHECK] Initializing NVS...");
  if (!NVSManager::init()) {
    Serial.println("WARNING: NVS init failed");
  }
  NVSConfig::begin();

  Serial.println("[CHECK] Initializing MCP...");
  MCPManager::getInstance().init();
  vTaskDelay(pdMS_TO_TICKS(200)); // Phase 2: Bus stabilization

  // ========================================================================
  // CRITICAL: V1 Parity - Boot Stabilization 
  // ========================================================================
  Serial.println("[BOOT] Device initialized.");

  // ========================================================================
  // CRITICAL: V1 Parity - Hardware Recovery Window (PRG Button)
  // ========================================================================
  if (MCPManager::getInstance().readPin(GPIO_PRG_BTN) == LOW) {
    Serial.println("[RECOVERY] PRG Button held!");
  }
  
  Serial.println("\n=== LoRaLink v2 Boot ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Board: %s / Radio: %s\n", HW_VERSION, RADIO_MODEL);
  Serial.printf("Free Heap: %u bytes\n", esp_get_free_heap_size());

  // Record boot timestamp
  g_bootTimestamp = millis();

  // Step 1: Initialize Display & Basic UI
  // ========================================================================
  Serial.println("\n[1/7] Initializing UI layer...");

  // Initialize Power & UI prerequisites
  // PowerManager already initialized above

  if (OLEDManager::getInstance().init()) {
    OLEDManager::getInstance().showSplash(FIRMWARE_VERSION, DEVICE_ROLE);
    Serial.println("  ✓ OLED/Power initialized");
  } else {
    Serial.println("  ! OLED init failed (non-fatal)");
  }

  // Initialize Product Engine & Sniffer mode configurations
  ProductManager::getInstance().init();
  ProbeManager::getInstance().init();

  OLEDManager::getInstance().drawBootProgress("SYSTEM START", 10);

  // Unify WiFi base initialization to prevent deadlocks in later transport setups
  Serial.println("  -> Initializing Network Stack...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(50));

  // Identity Extraction
  uint8_t mac_raw[6];
  esp_efuse_mac_get_default(mac_raw);
  char mac_buf[8];
  snprintf(mac_buf, sizeof(mac_buf), "[%02X:%02X]", mac_raw[4], mac_raw[5]);

  OLEDManager::getInstance().setMAC(mac_buf);
  OLEDManager::getInstance().setVersion(FIRMWARE_VERSION);
  OLEDManager::getInstance().setDeviceName(NVSManager::getNodeID("Node").c_str());
  OLEDManager::getInstance().setDiagnostics(NVSConfig::getBootCount(), NVSConfig::getResetReason().c_str());
  OLEDManager::getInstance().addLog("System Initialized");

  vTaskDelay(pdMS_TO_TICKS(10));

  // ========================================================================
  // Step 3: Initialize HAL (GPIO, SPI, etc.)
  // ========================================================================
  Serial.println("[3/8] Initializing HAL...");

  if (!radioHAL.init()) {
    Serial.println("ERROR: Radio HAL init failed!");
    while (1) vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelay(pdMS_TO_TICKS(200)); // SPI settling time

  relayHAL.init();
  OLEDManager::getInstance().drawBootProgress("HAL Core", 45);
  
  // Step 4: Initialize Plugins & Sensors
  // ========================================================================
  Serial.println("[4/8] Initializing plugins & sensors...");
  
  // Initialize Sensors
  #ifdef PIN_SENSOR_DHT
    uPinMode(PIN_SENSOR_DHT, INPUT); // Force use of universal mode
    static NativeDigitalIO dhtIO(PIN_SENSOR_DHT);
    static DHTSensorPlugin dhtPlugin(&dhtIO);
    sensorHAL.registerPlugin(&dhtPlugin);
  #endif
  sensorHAL.init();

  // Initialize all generic plugins
  // NutriCalc removed from core build
  pluginManager.initAll();

  Serial.println("  ✓ Core HAL & Plugins initialized");

  // Get Node ID early for diagnostics and BLE
  std::string nodeIDStr = NVSManager::getNodeID("Node");
  if (nodeIDStr.empty()) nodeIDStr = "Unknown";

  OLEDManager::getInstance().setDeviceName(nodeIDStr.c_str());

  // LoRa Transport
  if (!loraTransport.init()) {
    Serial.println("  ! LoRa transport init failed (check hardware)");
  } else {
    messageRouter.registerTransport(&loraTransport);
    Serial.println("  ✓ LoRa transport initialized");
  }
  vTaskDelay(pdMS_TO_TICKS(1000)); // Phase 3: LoRa settling delay (V1 parity)
  OLEDManager::getInstance().drawBootProgress("LORA COMM", 50);

  // Serial CLI Transport (Always active on Native USB)
  if (!serialTransport.init()) {
      Serial.println("  ! Serial CLI init failed");
  } else {
      Serial.println("  ✓ Serial CLI initialized");
      OLEDManager::getInstance().addLog("Serial CLI Ready");
  }
  OLEDManager::getInstance().drawBootProgress("SERIAL CLI", 60);

  // BLE Transport (Always active)
  vTaskDelay(pdMS_TO_TICKS(500)); 
  if (!BLETransport::initStatic()) {
      Serial.println("  ! BLE transport failed to start");
  } else {
      Serial.println("  ✓ BLE transport initialized");
      
      // Wire RX to CommandManager
      BLETransport::setRxCallback([](const String& data) {
          Serial.printf("[BLE] Command received: '%s'\n", data.c_str());
          CommandManager::process(data, [](const String& resp) {
              BLETransport::sendStringStatic(resp);
          });
      });
      OLEDManager::getInstance().addLog("BLE Ready");
  }
  OLEDManager::getInstance().drawBootProgress("BLE MESH", 65);
  
  // Phase 4: USB Stabilization delay (V1 Parity: 5000ms was used in V1, using 2000ms for V2 modern core)
  Serial.println("  -> Waiting for protocol stabilization...");
  vTaskDelay(pdMS_TO_TICKS(2000)); 

  // Optional WiFi Transport (graceful fallback if connection fails)
  std::string wifiSSID = NVSManager::getWiFiSSID();
  std::string wifiPass = NVSManager::getWiFiPassword();

  // Build mDNS hostname from node ID
  std::string mdnsHostname = "loralink-" + nodeIDStr;

  if (!wifiSSID.empty() && !wifiPass.empty()) {
    if (WiFiTransport::init(wifiSSID, wifiPass, mdnsHostname)) {
      Serial.println("  ✓ WiFi transport initialized (connecting...)");

      // Initialize HTTP API server (only if connected or AP started)
      if (HttpAPI::init()) {
        Serial.println("  ✓ HTTP API server started on port 80");
        OLEDManager::getInstance().setIP(WiFi.localIP().toString().c_str());
      } else {
        Serial.println("  ! HTTP API server failed to start (non-fatal)");
      }
    } else {
      Serial.println("  ! WiFi transport init failed, continuing without WiFi");
    }
  } else {
    Serial.println("  ! WiFi credentials not configured, skipping WiFi");
  }
  vTaskDelay(pdMS_TO_TICKS(500)); // Stagger between WiFi and ESP-NOW

  // Step 4.5: ESP-NOW Transport
  Serial.println("[4.5/8] Initializing ESP-NOW...");
  if (!espNowTransport.init()) {
      Serial.println("  ! ESP-NOW transport failed to start");
  } else {
      messageRouter.registerTransport(&espNowTransport);
      Serial.println("  ✓ ESP-NOW transport initialized");
  }
  OLEDManager::getInstance().drawBootProgress("TRANS PORTS", 90);

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

  // Optional BLE Transport (Legacy block removed, consolidated above)

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

  // Default Node ID for all Peers
  g_ourNodeID = 1; 
  Serial.println("  ✓ Peer mode initialized");

#ifdef HAS_GPS
  // Initialize GNSS (V4 support)
  GPSManager::init();
#endif

  meshCoordinator.init();
  meshCoordinator.setOwnNodeID(g_ourNodeID);
  meshCoordinator.setNeighborCallback([](uint8_t nodeId, bool isOnline) {
    #ifdef ENABLE_MQTT_TRANSPORT
      if (MQTTTransport::instance()->isConnected()) {
        MQTTTransport::instance()->publishNodeStatus(nodeId, isOnline);
      }
    #endif
  });
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
      vTaskDelay(pdMS_TO_TICKS(10));
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

  // Start Radio/Mesh/Probe tasks
  xTaskCreatePinnedToCore(radioTask, "RadioTask", 8192, NULL, 4, &radioTaskHandle, 1);
  xTaskCreatePinnedToCore(meshTask, "MeshTask", 8192, NULL, 3, &meshTaskHandle, 1);
  xTaskCreatePinnedToCore(probeTask, "ProbeTask", 4096, NULL, 2, &probeTaskHandle, 0); // Core 0 for radio-heavy

  // Register radioTask for interrupts
  RadioHAL::getInstance().setNotifyTask(radioTaskHandle);

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
  OLEDManager::getInstance().drawBootProgress("COMPLETE", 100);
  vTaskDelay(pdMS_TO_TICKS(10));

  // Print diagnostics
  PowerManager::printStatus();
  OLEDManager::getInstance().printStatus();

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
  // Drive OTA and WiFi reconnects (High frequency required during firmware upload)
  WiFiTransport::service();

  // Sleep minimally to give other tasks CPU time without starving OTA handling
  vTaskDelay(pdMS_TO_TICKS(10));

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
