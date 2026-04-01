#include <Arduino.h>
#include <string>
#include <cstdio>
#include <functional>
#include <WiFi.h>
#include <LittleFS.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>

#include "boot_sequence.h"
#include "nvs_manager.h"

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

#ifdef BENCH_MODE
  #include "bench_test.h"
#endif

// Forward declarations for tasks defined in main.cpp or separate handlers
extern void radioTask(void* param);
extern void meshTask(void* param);
extern void probeTask(void* param);
extern void displayTask(void* param);
extern void wifiTask(void* param);

// Global status handles
extern TaskHandle_t radioTaskHandle;
extern TaskHandle_t meshTaskHandle;
extern TaskHandle_t probeTaskHandle;
extern TaskHandle_t controlTaskHandle;
extern uint8_t g_ourNodeID;
extern uint32_t g_bootTimestamp;

// (Removed redundant g_i2cMutex definition)

void BootSequence::run() {
  initCore();
  vTaskDelay(pdMS_TO_TICKS(BOOT_SAFE_DELAY_STAGGER_MS));
  initHAL();
  vTaskDelay(pdMS_TO_TICKS(BOOT_SAFE_DELAY_STAGGER_MS));
  initTransports();
  vTaskDelay(pdMS_TO_TICKS(BOOT_SAFE_DELAY_STAGGER_MS));
  initApplication();
  createTasks();

  Serial.println("[8/8] Boot complete!");
  Serial.printf("  Uptime: %lu ms\n", millis() - g_bootTimestamp);
  OLEDManager::getInstance().drawBootProgress("COMPLETE", 100);
  vTaskDelay(pdMS_TO_TICKS(10));

  PowerManager::printStatus();
  OLEDManager::getInstance().printStatus();

  Serial.println("[9/9] Entering main loop (FreeRTOS)...");
  Serial.println("===========================\n");
}

void BootSequence::initCore() {
  // 1. Initialize serial IMMEDIATELY
  Serial.begin(SERIAL_BAUD);
  
  uint32_t startWait = millis();
  uint32_t waitTimeout = 1000; // v0.4.1-4: Reduced from 5000ms for faster boot
  
  while (!Serial && (millis() - startWait < waitTimeout)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  g_bootTimestamp = millis();

  Serial.println("\n[BOOT] Initializing Persistence...");
  if (!NVSManager::init()) {
    Serial.println("  ! NVS init failed");
  } else {
    Serial.println("  ✓ NVS Ready");
  }

  // Capture hardware reset reason immediately after NVS is available
  {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reasonStr = "UNKNOWN";
    switch (reason) {
      case ESP_RST_POWERON:  reasonStr = "POWERON";  break;
      case ESP_RST_EXT:      reasonStr = "EXT_PIN";  break;
      case ESP_RST_SW:       reasonStr = "SW_RESET"; break;
      case ESP_RST_PANIC:    reasonStr = "PANIC";    break;
      case ESP_RST_INT_WDT:  reasonStr = "INT_WDT";  break;
      case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT"; break;
      case ESP_RST_WDT:      reasonStr = "WDT";      break;
      case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT"; break;
      case ESP_RST_SDIO:     reasonStr = "SDIO";     break;
      default: break;
    }
    NVSManager::setResetReason(reasonStr);
    NVSManager::incrementBootCount();
    Serial.printf("[BOOT] Reset #%u — reason: %s\n",
                  NVSManager::getBootCount(), reasonStr);
  }

  if (!LittleFS.begin(true)) {
    Serial.println("  ! LittleFS mount failed");
  } else {
    Serial.println("  ✓ LittleFS Ready");
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.println("[TRACE] PowerManager::init()");
  PowerManager::init();
  
  // v0.4.0: V4 Hardware needs stable rail before I2C/OLED
  uint8_t stableRetry = 0;
  while (!PowerManager::isVEXTStable() && stableRetry < 20) {
    vTaskDelay(pdMS_TO_TICKS(50));
    stableRetry++;
  }
  vTaskDelay(pdMS_TO_TICKS(500)); // Enforce rail stability
  
  Serial.println("[CHECK] Initializing I2C Mutex...");
  if (!g_i2cMutex) {
      g_i2cMutex = xSemaphoreCreateMutex();
  }

  Serial.println("[CHECK] Initializing I2C Wire (100kHz)...");
  // Force 100kHz across all variants for V1-parity stability
  Wire.begin(I2C_SDA, I2C_SCL, 100000); 
  Wire.setTimeOut(100); // Prevent bus hangs on collision/error
  Serial.println("  ✓ I2C Wire started");

  // v0.4.0 EMERGENCY: Move OLED to front to reduce black-screen time
  // Hardened for V4: Double try with extra delay
  Serial.println("[v0.4.0] Priority OLED Init...");
  
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
  Serial.printf("[V4-TRACE] Initial VEXT pin: %d (Logic: LOW=ON, HIGH=OFF)\n", digitalRead(VEXT_PIN));
#endif

  bool oledOk = false;
  if (PluginManager::isEnabled("oled")) {
    oledOk = OLEDManager::getInstance().init();
    if (!oledOk) {
      Serial.println("  ! OLED Initial fail, retrying after VEXT toggle...");
      PowerManager::disableVEXT();
      vTaskDelay(pdMS_TO_TICKS(500));
      PowerManager::enableVEXT();
      vTaskDelay(pdMS_TO_TICKS(1000));
      oledOk = OLEDManager::getInstance().init();
    }
  }

  if (oledOk) {
    OLEDManager::getInstance().showSplash(FIRMWARE_VERSION, "BOOTING...");
    Serial.println("  ✓ OLED splash active");
  } else {
    Serial.printf("  ! OLED Hard Fail (VEXT: %d)\n", digitalRead(VEXT_PIN));
  }

#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
  Serial.printf("[V4] Activating Battery Sense (GPIO %d)...\n", BAT_ADC_CTRL);
  pinMode(BAT_ADC_CTRL, OUTPUT);
  digitalWrite(BAT_ADC_CTRL, HIGH); // V4 requires 37 HIGH to enable the divider

  Serial.printf("[V4] Enabling GPS Power (GPIO %d, Active LOW)...\n", GPS_EN_PIN);
  pinMode(GPS_EN_PIN, OUTPUT);
  digitalWrite(GPS_EN_PIN, LOW); // V4 GPS Power is Active LOW
  
  // Extra stabilization delay for V4 GPS UART
  vTaskDelay(pdMS_TO_TICKS(500));
#endif

  Serial.println("[CHECK] Initializing MCP...");
  // Restore core init (safe-mode guard implemented in HAL)
  MCPManager::getInstance().init();
  vTaskDelay(pdMS_TO_TICKS(200));

  // (Identity resolution moved to initCore to ensure OLED shows correct name immediately)
  ProductManager::getInstance().init();
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Board: %s / Radio: %s\n", HW_VERSION, RADIO_MODEL);
  Serial.printf("Free Heap: %u bytes\n", esp_get_free_heap_size());

  // [Standard] Identity Resolution (Enforce project identity before UI/OLED name display)
  String nodeIDStr = String(NVSManager::getNodeID("").c_str());
  
  // Only override if identity is a generic placeholder, missing, or null
  bool idInvalid = nodeIDStr.length() == 0 || nodeIDStr == "Unknown" || 
                   nodeIDStr == "Node" || nodeIDStr == "Peer" || 
                   nodeIDStr == "peer" || nodeIDStr == "Magic-Node";
  
  if (idInvalid) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    // Standard format: Magic-XXXX where XXXX is last 4 of MAC
    nodeIDStr = "Magic-";
    const char* hexChars = "0123456789ABCDEF";
    for (int i = 4; i < 6; i++) {
      nodeIDStr += hexChars[(mac[i] >> 4) & 0x0F];
      nodeIDStr += hexChars[mac[i] & 0x0F];
    }
    
    NVSManager::setNodeID(nodeIDStr.c_str());
    Serial.print("  ! Identity enforced (Standard): ");
    Serial.println(nodeIDStr);
  } else {
    Serial.print("  ✓ Identity preserved: ");
    Serial.println(nodeIDStr);
  }
}

void BootSequence::initHAL() {
  Serial.println("\n[1/7] UI Layer check...");
  if (PluginManager::isEnabled("oled")) {
    OLEDManager::getInstance().drawBootProgress("CORE READY", 10);
    Serial.println("  ✓ OLED status verified");
  } else {
    Serial.println("  ! OLED disabled in plugin config");
  }

  ProductManager::getInstance().init();
  ProbeManager::getInstance().init();
  OLEDManager::getInstance().drawBootProgress("SYSTEM START", 10);

  Serial.println("  -> Initializing Network Stack...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(50));

  uint8_t mac_raw[6];
  esp_efuse_mac_get_default(mac_raw);
  
  // Manual hex formatting for diagnostics to avoid missing snprintf
  char mac_buf[16];
  const char* hex = "0123456789ABCDEF";
  mac_buf[0] = '[';
  mac_buf[1] = hex[(mac_raw[4] >> 4) & 0x0F];
  mac_buf[2] = hex[mac_raw[4] & 0x0F];
  mac_buf[3] = ':';
  mac_buf[4] = hex[(mac_raw[5] >> 4) & 0x0F];
  mac_buf[5] = hex[mac_raw[5] & 0x0F];
  mac_buf[6] = ']';
  mac_buf[7] = '\0';

  OLEDManager::getInstance().setMAC(mac_buf);
  OLEDManager::getInstance().setVersion(FIRMWARE_VERSION);
  String currentName = String(NVSManager::getNodeID("Magic-Node").c_str());
  OLEDManager::getInstance().setDeviceName(currentName.c_str());
  OLEDManager::getInstance().addLog("System Initialized");
  OLEDManager::getInstance().setDiagnostics(NVSManager::getBootCount(), NVSManager::getResetReason().c_str());

  Serial.println("[3/8] Initializing Radio HAL...");
  if (!RadioHAL::getInstance().init()) {
    Serial.println("ERROR: Radio HAL init failed!");
    while (1) vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelay(pdMS_TO_TICKS(200)); 

  RelayHAL::getInstance().init();
  OLEDManager::getInstance().drawBootProgress("HAL Core", 45);
}

void BootSequence::initTransports() {
  Serial.println("[4/8] Initializing plugins & sensors...");
  
#ifdef PIN_SENSOR_DHT
  static NativeDigitalIO dhtIO(PIN_SENSOR_DHT);
  static DHTSensorPlugin dhtPlugin(&dhtIO);
  SensorHAL::getInstance().registerPlugin(&dhtPlugin);
#endif
  if (PluginManager::isEnabled("sensor")) {
    SensorHAL::getInstance().init();
  }
  
  if (PluginManager::isEnabled("mcp")) {
     MCPManager::getInstance().init();
  }

  PluginManager::getInstance().initAll();
  Serial.println("  ✓ Core HAL & Plugins initialized");

  // Core Transport: LoRa (Enforced ON by Default)
  if (!LoRaTransport::getInstance().init()) {
    Serial.println("  ! LoRa transport failed init");
  } else {
    MessageRouter::instance().registerTransport((TransportInterface*)&LoRaTransport::getInstance());
    Serial.println("  ✓ LoRa transport active (Default)");
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
  OLEDManager::getInstance().drawBootProgress("LORA COMM", 50);

  if (!SerialTransport::getInstance().init()) {
    Serial.println("  ! Serial CLI failed");
  } else {
    Serial.println("  ✓ Serial CLI initialized");
    OLEDManager::getInstance().addLog("Serial CLI Ready");
  }
  OLEDManager::getInstance().drawBootProgress("SERIAL CLI", 60);

  vTaskDelay(pdMS_TO_TICKS(500));
  if (PluginManager::isEnabled("ble") && BLETransport::initStatic()) {
    Serial.println("  ✓ BLE transport initialized");
    BLETransport::setRxCallback([&](const String& data) {
      Serial.print("[BLE] Command received: '");
      Serial.print(data);
      Serial.println("'");
      CommandManager::process(data, [](const String& resp) {
        BLETransport::sendStringStatic(resp);
      });
    });
    OLEDManager::getInstance().addLog("BLE Ready");
  } else {
    Serial.println("  - BLE skipped");
  }
  vTaskDelay(pdMS_TO_TICKS(2000)); // 0.4.0 Stabilization: BLE protocol settle
  OLEDManager::getInstance().drawBootProgress("BLE MESH", 65);

  vTaskDelay(pdMS_TO_TICKS(2000)); 

  String currentID = String(NVSManager::getNodeID("").c_str());
  String wifiSSID = String(NVSManager::getWiFiSSID().c_str());
  String wifiPass = String(NVSManager::getWiFiPassword().c_str());
  String mdnsHostname = "magic-" + currentID;

  // Core Transport: WiFi (Enforced ON by Default - STA with Fallback to AP)
  if (WiFiTransport::init(wifiSSID.c_str(), wifiPass.c_str(), mdnsHostname.c_str())) {
    Serial.println("  ✓ WiFi transport active (Default)");
  } else {
    Serial.println("  - WiFi STA failed, using SoftAP for provisioning");
  }
  
#ifdef ENABLE_HTTP_API
  if (HttpAPI::init()) {
    Serial.println("  ✓ HTTP API server started");
    OLEDManager::getInstance().setIP(WiFi.localIP().toString().c_str());
  }
#endif

  vTaskDelay(pdMS_TO_TICKS(500));
  Serial.println("[4.5/8] Initializing ESP-NOW...");
  if (PluginManager::isEnabled("espnow") && ESPNowTransport::getInstance().init()) {
    // Explicit pointer cast to TransportInterface*
    MessageRouter::instance().registerTransport((TransportInterface*)&ESPNowTransport::getInstance());
    ESPNowTransport::getInstance().startDiscovery();
    Serial.println("  ✓ ESP-NOW transport initialized + Discovery Active");
  } else {
    Serial.println("  - ESP-NOW skipped");
  }
  OLEDManager::getInstance().drawBootProgress("TRANS PORTS", 90);

  CommandManager::begin();
  CommandManager::setRelayCallback([](uint8_t relay, bool state) {
    uint8_t mask = RelayHAL::getInstance().getState();
    uint8_t bit = relay - 1;
    if (state) mask |= (1u << bit);
    else       mask &= ~(1u << bit);
    RelayHAL::getInstance().setState(mask);
  });

#ifdef ENABLE_MQTT_TRANSPORT
  if (PluginManager::isEnabled("mqtt") && MQTTTransport::initStatic()) {
    MQTTTransport::onCommand([](const String& cmd) {
      CommandManager::process(cmd, [](const String& resp) {
        MQTTTransport::instance()->publishResponse(resp);
      });
    });
  }
#endif
}

void BootSequence::initApplication() {
  Serial.println("[5/8] Initializing application...");

  g_ourNodeID = 1;
  Serial.println("  ✓ Peer mode initialized");

#ifdef HAS_GPS
  if (PluginManager::isEnabled("gps")) {
    GPSManager::init();
  }
#endif

  MeshCoordinator::instance().init();
  MeshCoordinator::instance().setOwnNodeID(g_ourNodeID);
  MeshCoordinator::instance().setNeighborCallback([](uint8_t nodeId, bool isOnline) {
#ifdef ENABLE_MQTT_TRANSPORT
    if (MQTTTransport::instance()->isConnected()) {
      MQTTTransport::instance()->publishNodeStatus(nodeId, isOnline);
    }
#endif
  });
  Serial.println("  ✓ Mesh coordinator initialized");

  MessageRouter::instance().setMessageHandler(MessageHandler::handleReceived);

#ifdef BENCH_MODE
  Serial.println("[6/8] Running bench mode diagnostics...");
  if (!BenchTest::runAll()) {
    Serial.println("\n  WARNING: Some bench tests failed!");
  }
  Serial.println("[6/8] Re-initializing hardware after bench mode...");
  RadioHAL::getInstance().init();
  RelayHAL::getInstance().init();
  LoRaTransport::getInstance().init();
#endif

  if (PluginManager::isEnabled("scheduler") && ScheduleManager::init()) {
    Serial.printf("  ✓ Scheduler initialized (%d task(s) loaded)\n",
                  ScheduleManager::getTaskCount());
  }

  MsgManager::getInstance().init();
  Serial.println("  ✓ MsgManager (LMX) initialized");
}

void BootSequence::createTasks() {
  Serial.println("[7/8] Creating FreeRTOS tasks...");
  xTaskCreatePinnedToCore(radioTask, "RadioTask", 8192, NULL, 4, &radioTaskHandle, 0); // Core 0
  xTaskCreatePinnedToCore(meshTask, "MeshTask", 8192, NULL, 3, &meshTaskHandle, 0);   // Core 0
  xTaskCreatePinnedToCore(probeTask, "ProbeTask", 4096, NULL, 2, &probeTaskHandle, 0);

  RadioHAL::getInstance().setNotifyTask(radioTaskHandle);

  xTaskCreatePinnedToCore(
    ControlLoop::execute,
    "Control",
    4096,
    nullptr,
    2,
    &controlTaskHandle,
    1
  );

  // Display update task (low priority to not starve control loop)
  xTaskCreatePinnedToCore(displayTask, "DisplaySvc", 4096, NULL, 1, NULL, 1);

  // WiFi/MQTT service task (low priority to not starve control loop)
  xTaskCreatePinnedToCore(wifiTask, "WiFiSvc", 4096, NULL, 1, NULL, 1);

  Serial.println("  ✓ Tasks created");
}
