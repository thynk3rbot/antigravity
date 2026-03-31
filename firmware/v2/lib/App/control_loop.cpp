/**
 * @file control_loop.cpp
 * @brief Implementation of periodic system maintenance
 */

#include "control_loop.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// App Logic
#include "power_manager.h"
#include "oled_manager.h"
#include "nvs_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include "mesh_coordinator.h"
#include "command_manager.h"
#include "schedule_manager.h"
#include "gps_manager.h"
#include "plugin_manager.h"
#include "status_builder.h"
#include "control_packet.h"
#include "../HAL/relay_hal.h"
#include "../HAL/sensor_hal.h"

// Transport Layer
#include "../Transport/lora_transport.h"
#include "../Transport/wifi_transport.h"
#include "../Transport/ble_transport.h"
#include "../Transport/mqtt_transport.h"
#include "../Transport/espnow_transport.h"
#include "../Transport/message_router.h"

extern uint32_t g_bootTimestamp;
extern uint8_t g_ourNodeID;

// Static member initialization
uint16_t ControlLoop::cachedTempC_x10 = 2500;

void ControlLoop::execute(void* param) {
  while (1) {
    updatePower();
    updateTelemetry();
    updateOLED(); // Data update (500ms)
    updateStatusRegistry();
    updateMesh();
    runDiscoveryBeacons();
    pollPlugins();

    OLEDManager::getInstance().update(); // UI/Button Poll (10ms)
    vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz loop
  }
}

void ControlLoop::updatePower() {
  static uint32_t lastPowerUpdate = 0;
  static const uint32_t POWER_UPDATE_INTERVAL_MS = 30000;
  uint32_t now = millis();

  if (now - lastPowerUpdate >= POWER_UPDATE_INTERVAL_MS) {
    PowerManager::update();
    lastPowerUpdate = now;
  }
}

void ControlLoop::updateTelemetry() {
  static uint32_t lastTelemetrySend = 0;
  static const uint32_t SEND_TELEMETRY_INTERVAL_MS = 10000;
  uint32_t now = millis();

  if (now - lastTelemetrySend >= SEND_TELEMETRY_INTERVAL_MS) {
    // Use cached sensor data (read once per 1s in updateOLED)
    uint16_t voltageV_x100 = static_cast<uint16_t>(PowerManager::getBatteryVoltage() * 100.0f);
    uint8_t relayState = RelayHAL::getInstance().getState();

    ControlPacket telemetry = ControlPacket::makeTelemetry(
      g_ourNodeID, 0xFF, cachedTempC_x10, voltageV_x100, relayState,
      LoRaTransport::getInstance().getSignalStrength()
    );

    MessageRouter::instance().broadcastPacket((uint8_t*)&telemetry, sizeof(telemetry));
    lastTelemetrySend = now;
  }
}

void ControlLoop::updateOLED() {
  static uint32_t lastOLEDUpdate = 0;
  static const uint32_t OLED_UPDATE_INTERVAL_MS = 1000; // Slow down telemetry updates
  uint32_t now = millis();

  if (now - lastOLEDUpdate >= OLED_UPDATE_INTERVAL_MS) {
    OLEDManager& oled = OLEDManager::getInstance();

    oled.setUptime(now - g_bootTimestamp);
    oled.setFreeHeap(esp_get_free_heap_size());

    // Battery & power mode
    const char* modeStr = "NORMAL";
    switch (PowerManager::getMode()) {
      case PowerMode::CONSERVE: modeStr = "CONSERVE"; break;
      case PowerMode::CRITICAL: modeStr = "CRITICAL"; break;
      default: break;
    }
    oled.setBatteryVoltage(PowerManager::getBatteryVoltage(), modeStr);

    // Radio signal (v2 aligns with v0.0.11 schema)
    oled.setLoRaSignal(LoRaTransport::getInstance().getSignalStrength(), 
                        LoRaTransport::getInstance().getLastSNR());

    // Transport status
    oled.setTransportStatus(
      WiFi.isConnected(),
      BLETransport::isConnected(),
#ifdef ENABLE_MQTT_TRANSPORT
      MQTTTransport::instance()->isConnected(),
#else
      false,
#endif
      LoRaTransport::getInstance().isReady(),
      ESPNowTransport::getInstance().isReady()
    );

    // Relay, temperature, peers
    oled.setRelayStatus(RelayHAL::getInstance().getState());  // full 8-ch bitmask

    auto sensorData = SensorHAL::getInstance().readAll();
    for (const auto& r : sensorData) {
      if (r.type == SensorType::TEMPERATURE) {
        oled.setTemperature(r.value);
        // Cache for telemetry (avoid duplicate sensor reads)
        cachedTempC_x10 = static_cast<uint16_t>(r.value * 10.0f);
        break;
      }
    }

    oled.setPeerCount(MeshCoordinator::instance().getNeighborCount());

#ifdef HAS_GPS
    {
      auto gps = GPSManager::getData();
      oled.setGPS(gps.lat, gps.lon, gps.satellites, gps.hasFix);
    }
#endif

    // NOTE: oled.update() now called at 100Hz in main loop for button sensitivity
    lastOLEDUpdate = now;
  }
}

void ControlLoop::updateStatusRegistry() {
  static uint32_t lastRegistryUpdate = 0;
  uint32_t now = millis();

  if (now - lastRegistryUpdate >= 5000) {
    CommandManager::StatusData status;
    status.nodeId = String(NVSManager::getNodeID("Node").c_str());
    status.meshId = g_ourNodeID;
    status.version = FIRMWARE_VERSION; 
    status.hw = HW_VERSION;
    status.mac = WiFi.macAddress();
    status.ipAddr = WiFi.localIP().toString();
    status.batVoltage = PowerManager::getBatteryVoltage();
    status.batPercent = PowerManager::getBatteryPercent();
    
    switch (PowerManager::getMode()) {
      case PowerMode::NORMAL:   status.powerMode = "NORMAL"; break;
      case PowerMode::CONSERVE: status.powerMode = "CONSERVE"; break;
      case PowerMode::CRITICAL: status.powerMode = "CRITICAL"; break;
      default:                  status.powerMode = "UNKNOWN"; break;
    }

    status.loraRSSI = LoRaTransport::getInstance().getSignalStrength();
    status.meshNeighbors = MeshCoordinator::instance().getNeighborCount();
    status.uptime = now - g_bootTimestamp;
    status.freeHeap = esp_get_free_heap_size();

    CommandManager::updateStatus(status);
    lastRegistryUpdate = now;
  }
}

void ControlLoop::updateMesh() {
  static uint32_t lastMeshAgeOut = 0;
  uint32_t now = millis();

  if (now - lastMeshAgeOut > 60000) {
    MeshCoordinator::instance().ageOutNeighbors();
    lastMeshAgeOut = now;
  }
  ScheduleManager::execute();
}

void ControlLoop::runDiscoveryBeacons() {
  static uint32_t lastBeaconMs = 0;
  uint32_t now = millis();

  // EVERY 5 SECONDS: Broadcast Autonomous Mesh HELLO (ESP-NOW)
  if (now - lastBeaconMs > 5000) {
    // HELLO beacon contains: {type: HEARTBEAT, hw_ver, caps}
    ControlPacket hello = ControlPacket::makeHeartbeat(g_ourNodeID);
    ESPNowTransport::getInstance().send((uint8_t*)&hello, sizeof(hello));
    
    lastBeaconMs = now;
    Serial.println("[MESH] Discovery beacon sent (ESP-NOW)");
  }
}

void ControlLoop::pollPlugins() {
  PluginManager::getInstance().pollAll();
#ifdef ENABLE_MQTT_TRANSPORT
  MQTTTransport::instance()->isConnected(); // Placeholder for poll
#endif
#ifdef HAS_GPS
  GPSManager::update();
#endif
}
