// ============================================================================
//  LoRaLink-AnyToAny — Unified Wireless Communication Framework
//  Main Firmware for Heltec WiFi LoRa 32 V3
//  (c) 2026 Steven P Williams (spw1.com)
// ============================================================================

#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

// Managers
#include "managers/BLEManager.h"
#include "managers/CommandManager.h"
#include "managers/DataManager.h"
#include "managers/DisplayManager.h"
#include "managers/ESPNowManager.h"
#include "managers/LoRaManager.h"
// #include "managers/MCPManager.h"  // DISABLED — not on this board
#include "managers/MQTTManager.h"
#include "managers/PerformanceManager.h"
#include "managers/ProductManager.h"
#include "managers/ScheduleManager.h"
#include "managers/WiFiManager.h"
#include "managers/GPSManager.h"
#include "utils/DebugMacros.h"

#ifndef UNIT_TEST
// ============================================================================
//   BOOT SEQUENCE
// ============================================================================
void setup() {
  // 1. Serial & Heartbeat
  pinMode(35, OUTPUT);
  for(int i=0; i<10; i++){
    digitalWrite(35, LOW); delay(50); digitalWrite(35, HIGH); delay(50);
  }
  Serial.begin(115200);
  delay(2000);
  LOG_PRINTLN("\n\n>>> LORALINK DEBUG BOOT START <<<");
  LOG_PRINTLN("\n\n########################################");
  LOG_PRINTLN("#  LORALINK-ANY2ANY " FIRMWARE_VERSION "             #");
  LOG_PRINTLN("#  Unified Wireless Gateway            #");
  LOG_PRINTLN("########################################");
  LOG_PRINTLN("BOOT: Serial Started.");

  // 2. PRG button factory reset window
  pinMode(PIN_BUTTON_PRG, INPUT_PULLUP);
  LOG_PRINTLN("BOOT: Hold PRG for 3s to factory reset...");
  unsigned long windowStart = millis();
  unsigned long pressStart = 0;
  bool pressing = false;
  bool resetTriggered = false;
  while (millis() - windowStart < 5000) {
    if (digitalRead(PIN_BUTTON_PRG) == LOW) {
      if (!pressing) {
        pressing = true;
        pressStart = millis();
      }
      if (millis() - pressStart >= 3000) {
        resetTriggered = true;
        break;
      }
    } else {
      pressing = false;
    }
    delay(10);
  }
  if (resetTriggered) {
    LOG_PRINTLN("BOOT: Factory Reset triggered!");
    DataManager::getInstance().FactoryReset();
    delay(1000);
    ESP.restart();
  }

  // 3. Power rail initialization (PREPARE only)
  LOG_PRINTF("BOOT: VEXT PIN = %d, BAT PIN = %d\n", PIN_VEXT_CTRL, PIN_BAT_CTRL);
  if (PIN_VEXT_CTRL != -1) {
    LOG_PRINTLN("BOOT: Setting VEXT pinMode...");
    pinMode(PIN_VEXT_CTRL, OUTPUT);
    LOG_PRINTLN("BOOT: Setting VEXT Level...");
    digitalWrite(PIN_VEXT_CTRL, HIGH); // Start with Display/LoRa OFF (HIGH = OFF on V2 and V3 default)
  }
  if (PIN_BAT_CTRL != -1) {
    LOG_PRINTLN("BOOT: Setting BAT pinMode...");
    pinMode(PIN_BAT_CTRL, OUTPUT);
    LOG_PRINTLN("BOOT: Setting BAT Level...");
    digitalWrite(PIN_BAT_CTRL, LOW); // Power ON battery divider
  }
  LOG_PRINTLN("BOOT: Delaying 500ms...");
  delay(500);

  // 4. CPU clock optimization
  LOG_PRINTF("SYS: CPU Clock = %dMHz\n", getCpuFrequencyMhz());

#ifdef ARDUINO_LORA_HELTEC_V2
  // 5. Heltec init (V2) - handles Vext, Display, and Serial.
  // Display=true, LoRa=false (we handle via RadioLib), Serial=true, PABOOST=true, Freq=0
  Heltec.begin(true, false, true, true, 0);
  if (Heltec.display) {
    Heltec.display->setContrast(255);
    Heltec.display->setBrightness(255);
  }
#else
  // 5. Heltec init (V3)
  Heltec.begin(true, false, true, false, 0);
  if (Heltec.display) {
    Heltec.display->setContrast(255);
    Heltec.display->setBrightness(255);
  }
#endif
  LOG_PRINTLN("\n\n>>> LORALINK DEBUG BOOT START <<<");

  // 6. Data Manager
  LOG_PRINTLN("BOOT: DataManager...");
  DataManager &data = DataManager::getInstance();
  data.Init();

  // 6.1 Performance Manager
  PerformanceManager::getInstance().init();

  LOG_PRINTLN("ID: " + data.myId + " [VAL:" + data.getMacSuffix() + "]");

  // 6.6. Product Manager - restore active product pin modes
  Serial.println("BOOT: ProductManager...");
  ProductManager::getInstance().restoreActiveProduct();
  Serial.flush();

  // 7. Command Manager - restore hardware state
  Serial.println("BOOT: CommandManager...");
  CommandManager::getInstance().restoreHardwareState();
  Serial.flush();

  // --- SAFE POWER STAGGER ---
  Serial.println("SYS: Powering ON Peripherals (VExt)...");
  if (PIN_VEXT_CTRL != -1) {
    pinMode(PIN_VEXT_CTRL, OUTPUT);
    digitalWrite(PIN_VEXT_CTRL, LOW); // Power ON Display/LoRa/VC6580 (LOW = ON)
  }
  delay(1000);

  // 8. LoRa Manager
  Serial.println("BOOT: LoRaManager...");
  LoRaManager::getInstance().Init();
  LoRaManager::SetCallback([](const String &msg, CommInterface ifc) {
    CommandManager::getInstance().handleCommand(msg, ifc);
  });

  // --- SAFE POWER STAGGER ---
  Serial.println("SYS: Waiting for USB stabilize... (5000ms)");
  delay(5000);

  // 9. BLE Manager (Step 3: Re-enabled)
  Serial.println("BOOT: BLEManager...");
  BLEManager::getInstance().init();

#ifdef SUPPORT_WIFI
  // 10. WiFi Manager
  Serial.println("BOOT: WiFiManager...");
  WiFiManager::getInstance().init();

  // 11.5 MQTT Manager
  Serial.println("BOOT: MQTTManager...");
  MQTTManager::getInstance().Init();
#endif

#ifdef SUPPORT_ESPNOW
  // 11. ESP-NOW Manager
  Serial.println("BOOT: ESPNowManager...");
  ESPNowManager::getInstance().init();
#endif

#ifdef SUPPORT_DISPLAY
  // 12. Display Manager
  Serial.println("BOOT: DisplayManager...");
  DisplayManager::getInstance().Init();
#endif

  // 13. Schedule Manager
  Serial.println("BOOT: ScheduleManager...");
  ScheduleManager::getInstance().init();

  // 14. GPS Manager
#ifdef SUPPORT_GPS
  Serial.println("BOOT: GPSManager...");
  GPSManager::getInstance().init();
#endif

  Serial.println("SYS: Master Unit Boot (OLED+BLE Active)");
  Serial.println("########################################\n");
}

// ============================================================================
//   MAIN LOOP
// ============================================================================
void loop() {
  PerformanceManager::getInstance().loopTickStart();

  ScheduleManager::getInstance().execute();
#ifdef SUPPORT_WIFI
  MQTTManager::getInstance().loop();
#endif

#ifdef SUPPORT_GPS
  GPSManager::getInstance().loop();
#endif

  static unsigned long lastTic = 0;
  if (millis() - lastTic > 5000) {
    lastTic = millis();
    Serial.println("SYS: Alive (Heartbeat)");
  }

  PerformanceManager::getInstance().loopTickEnd();
}
#endif
