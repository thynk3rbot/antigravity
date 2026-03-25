/**
 * @file boot_sequence.cpp
 * @brief Implementation of device startup logic
 */

#include "boot_sequence.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// HAL Layer
#include "../HAL/board_config.h"
#include "../HAL/mcp_manager.h"
#include "../HAL/relay_hal.h"
#include "../HAL/probe_manager.h"
#include "../HAL/sensor_hal.h"
#include "../HAL/radio_hal.h"

// Transport Layer
#include "../Transport/message_router.h"
#include "../Transport/lora_transport.h"
#include "../Transport/wifi_transport.h"
#include "../Transport/ble_transport.h"
#include "../Transport/mqtt_transport.h"
#include "../Transport/serial_transport.h"
#include "../Transport/espnow_transport.h"

// Application Layer
#include "nvs_manager.h"
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
#include "control_loop.h"

// Forward declarations for tasks defined in main.cpp or separate handlers
extern void radioTask(void* param);
extern void meshTask(void* param);
extern void probeTask(void* param);

// Global status handles
extern TaskHandle_t radioTaskHandle;
extern TaskHandle_t meshTaskHandle;
extern TaskHandle_t probeTaskHandle;
extern TaskHandle_t controlTaskHandle;
extern uint8_t g_ourNodeID;
extern uint32_t g_bootTimestamp;

void BootSequence::run() {
  initCore();
  initHAL();
  initTransports();
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
  while (!Serial && (millis() - startWait < 3000)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  g_bootTimestamp = millis();

  Serial.println("\n[BOOT] Initializing Persistence...");
  if (!NVSManager::init()) {
    Serial.println("  ! NVS init failed");
  } else {
    Serial.println("  ✓ NVS Ready");
  }

  if (!LittleFS.begin(true)) {
    Serial.println("  ! LittleFS mount failed");
  } else {
    Serial.println("  ✓ LittleFS Ready");
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.println("[TRACE] PowerManager::init()");
  PowerManager::init();
  Serial.println("[TRACE] PowerManager::enableVEXT()");
  PowerManager::enableVEXT();
  vTaskDelay(pdMS_TO_TICKS(500)); 
  
  Serial.println("[CHECK] Initializing I2C...");
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
  Wire.begin(I2C_SDA, I2C_SCL, 100000); 
#else
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
#endif
  
  MCPManager::getInstance().init();
  vTaskDelay(pdMS_TO_TICKS(200)); 

  Serial.println("\n=== LoRaLink v2 Boot ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Board: %s / Radio: %s\n", HW_VERSION, RADIO_MODEL);
}

void BootSequence::initHAL() {
  Serial.println("\n[1/7] Initializing UI layer...");
  if (OLEDManager::getInstance().init()) {
    OLEDManager::getInstance().showSplash(FIRMWARE_VERSION, DEVICE_ROLE);
    Serial.println("  ✓ OLED initialized");
  } else {
    Serial.println("  ! OLED init failed");
  }

  ProductManager::getInstance().init();
  ProbeManager::getInstance().init();
  OLEDManager::getInstance().drawBootProgress("SYSTEM START", 10);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(50));

  uint8_t mac_raw[6];
  esp_efuse_mac_get_default(mac_raw);
  char mac_buf[16];
  snprintf(mac_buf, sizeof(mac_buf), "[%02X:%02X]", mac_raw[4], mac_raw[5]);

  OLEDManager::getInstance().setMAC(mac_buf);
  OLEDManager::getInstance().setVersion(FIRMWARE_VERSION);
  OLEDManager::getInstance().setDeviceName(NVSManager::getNodeID("Node").c_str());
  OLEDManager::getInstance().addLog("System Initialized");

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
  SensorHAL::getInstance().init();
  PluginManager::getInstance().initAll();

  std::string nodeIDStr = NVSManager::getNodeID("Node");
  if (nodeIDStr.empty()) nodeIDStr = "Unknown";

  if (!LoRaTransport::getInstance().init()) {
    Serial.println("  ! LoRa transport failed");
  } else {
    MessageRouter::instance().registerTransport(&LoRaTransport::getInstance());
  }
  vTaskDelay(pdMS_TO_TICKS(1000)); 
  OLEDManager::getInstance().drawBootProgress("LORA COMM", 50);

  if (!SerialTransport::getInstance().init()) {
    Serial.println("  ! Serial CLI failed");
  } else {
    Serial.println("  ✓ Serial CLI initialized");
  }

  vTaskDelay(pdMS_TO_TICKS(500)); 
  if (!BLETransport::initStatic()) {
    Serial.println("  ! BLE transport failed");
  } else {
    BLETransport::setRxCallback([](const String& data) {
      CommandManager::process(data, [](const String& resp) {
        BLETransport::sendStringStatic(resp);
      });
    });
  }

  vTaskDelay(pdMS_TO_TICKS(2000)); 

  std::string wifiSSID = NVSManager::getWiFiSSID();
  std::string wifiPass = NVSManager::getWiFiPassword();
  std::string mdnsHostname = "loralink-" + nodeIDStr;

  if (!wifiSSID.empty() && !wifiPass.empty()) {
    if (WiFiTransport::init(wifiSSID, wifiPass, mdnsHostname)) {
      if (HttpAPI::init()) {
        OLEDManager::getInstance().setIP(WiFi.localIP().toString().c_str());
      }
    }
  }

  vTaskDelay(pdMS_TO_TICKS(500));
  if (!ESPNowTransport::getInstance().init()) {
    Serial.println("  ! ESP-NOW failed");
  } else {
    MessageRouter::instance().registerTransport(&ESPNowTransport::getInstance());
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
  if (MQTTTransport::initStatic()) {
    MQTTTransport::onCommand([](const std::string& cmd) {
      CommandManager::process(String(cmd.c_str()), [](const String& resp) {
        MQTTTransport::instance()->publishResponse(resp);
      });
    });
  }
#endif
}

void BootSequence::initApplication() {
  g_ourNodeID = 1; 
  
#ifdef HAS_GPS
  GPSManager::init();
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

  MessageRouter::instance().setMessageHandler(MessageHandler::handleReceived);

  ScheduleManager::init();
}

void BootSequence::createTasks() {
  xTaskCreatePinnedToCore(radioTask, "RadioTask", 8192, NULL, 4, &radioTaskHandle, 1);
  xTaskCreatePinnedToCore(meshTask, "MeshTask", 8192, NULL, 3, &meshTaskHandle, 1);
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
}
