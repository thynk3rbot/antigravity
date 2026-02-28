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
#include "managers/ScheduleManager.h"
#include "managers/WiFiManager.h"

// ============================================================================
//   BOOT SEQUENCE
// ============================================================================
void setup() {
  // 1. Serial
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n########################################");
  Serial.println("#  LORALINK-ANYTOANY " FIRMWARE_VERSION "             #");
  Serial.println("#  Unified Wireless Gateway            #");
  Serial.println("########################################");

  // 2. PRG button factory reset window
  pinMode(PIN_BUTTON_PRG, INPUT_PULLUP);
  Serial.println("BOOT: Hold PRG for 3s to factory reset...");
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
    Serial.println("BOOT: Factory Reset triggered!");
    DataManager::getInstance().FactoryReset();
    delay(1000);
    ESP.restart();
  }

  // 3. Power rail initialization (critical for Heltec V3 OLED)
  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW); // Power ON display rail
  delay(100);

  // 4. CPU clock optimization (prevent brownouts)
  setCpuFrequencyMhz(80);
  Serial.printf("SYS: CPU Clock = %dMHz\n", getCpuFrequencyMhz());

  // 5. Heltec init (display only, LoRa handled by LoRaManager)
  Heltec.begin(true, false, true, false, 0);

  // 6. Data Manager
  Serial.println("BOOT: DataManager...");
  DataManager &data = DataManager::getInstance();
  data.Init();
  Serial.println("ID: " + data.myId + " [VAL:" + data.getMacSuffix() + "]");
  Serial.flush();

  // 7. Command Manager - restore hardware state
  Serial.println("BOOT: Restoring hardware state...");
  CommandManager::getInstance().restoreHardwareState();
  Serial.flush();

  // 8. LoRa Manager
  Serial.println("BOOT: LoRaManager...");
  LoRaManager::getInstance().Init();
  LoRaManager::SetCallback([](const String &msg, CommInterface ifc) {
    CommandManager::getInstance().handleCommand(msg, ifc);
  });
  Serial.flush();

  // 9. BLE Manager (deferred to ScheduleManager task for staggered start)
  Serial.println("BOOT: BLEManager... (Deferred)");
  Serial.flush();

  // 10. WiFi Manager
  Serial.println("BOOT: WiFiManager...");
  WiFiManager::getInstance().init();
  setWebCallback([](const String &cmd, CommInterface ifc) {
    CommandManager::getInstance().handleCommand(cmd, ifc);
  });
  Serial.flush();

  // 11. ESP-NOW Manager
  Serial.println("BOOT: ESPNowManager...");
  ESPNowManager::getInstance().init();
  Serial.flush();

  // 12. Display Manager
  Serial.println("BOOT: DisplayManager...");
  DisplayManager::getInstance().Init();
  Serial.flush();

  // 13. Schedule Manager - start all tasks
  Serial.println("BOOT: ScheduleManager...");
  ScheduleManager::getInstance().init();
  Serial.flush();

  Serial.println("BOOT: Setup OK — Entering Event Loop.");
  Serial.printf("BOOT: Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println("########################################\n");
}

// ============================================================================
//   MAIN LOOP
// ============================================================================
void loop() { ScheduleManager::getInstance().execute(); }
