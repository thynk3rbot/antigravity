#include "DisplayManager.h"
#include "PowerManager.h"
#include "../utils/DebugMacros.h"
#include "DataManager.h"
#include "heltec.h"
#ifdef SUPPORT_WIFI
#include <WiFi.h>
#endif

DisplayManager::DisplayManager() {
  currentPage = 0;
  displayActive = true;
  lastDisplayActivity = 0;
  lastButtonPress = 0;
  batteryVolts = 0;
}

void DisplayManager::Init() {
  Serial.println("Display: VEXT Powering ON...");
  Serial.flush();

  if (PIN_VEXT_CTRL != -1) {
    pinMode(PIN_VEXT_CTRL, OUTPUT);
    digitalWrite(PIN_VEXT_CTRL, LOW); // LOW = ON for most Heltec displays
  }
  if (PIN_BAT_CTRL != -1) {
    pinMode(PIN_BAT_CTRL, OUTPUT);
    digitalWrite(PIN_BAT_CTRL, LOW);
  }
  delay(100);

  displayActive = true;
  lastDisplayActivity = millis();
  Serial.println("Display: VEXT OK");
  Serial.flush();

  if (Heltec.display) {
    Heltec.display->setContrast(255);
    Heltec.display->setBrightness(255);
  }
#if defined(ARDUINO_LORA_HELTEC_V2)
  // Pulse VEXT for V2 display stability
  if (VEXT != -1) {
      pinMode(VEXT, OUTPUT);
      digitalWrite(VEXT, LOW); delay(50);
      digitalWrite(VEXT, HIGH); delay(50);
      digitalWrite(VEXT, LOW);
  }
#endif
  ShowSplash();
  delay(1500);
}

void DisplayManager::ShowSplash() {
  if (Heltec.display == NULL)
    return;
  DataManager &data = DataManager::getInstance();
  Heltec.display->clear();
  Heltec.display->setColor(WHITE);

  // Industrial HUD Outline
  Heltec.display->drawRect(0, 0, 128, 64);
  Heltec.display->drawLine(0, 15, 128, 15);
  Heltec.display->drawLine(0, 48, 128, 48);

  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->drawString(64, 18, "LoRaLink");

  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(64, 34, "Any2Any Suite");

  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(5, 2, "SYSTEM BOOT");
  Heltec.display->drawString(5, 50, "B:" + String(data.bootCount));

  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->drawString(123, 2, FIRMWARE_VERSION);
  Heltec.display->drawString(123, 50, "R:" + data.getResetReason());

  Heltec.display->display();
}

void DisplayManager::SetDisplayActive(bool active) {
  displayActive = active;
  if (Heltec.display == NULL)
    return;
  if (active) {
    Heltec.display->displayOn();
    lastDisplayActivity = millis();
  } else {
    Heltec.display->displayOff();
  }
}

bool DisplayManager::IsDisplayActive() { return displayActive; }

void DisplayManager::NextPage() {
  currentPage = (currentPage + 1) % NUM_PAGES;
  LOG_PRINTF("DISP: NextPage() -> Page %d\n", currentPage);
  SetDisplayActive(true);
}

void DisplayManager::DrawUi() {
  if (!displayActive || Heltec.display == NULL)
    return;

  DataManager &data = DataManager::getInstance();

  Heltec.display->clear();
  Heltec.display->setColor(WHITE);

  batteryVolts = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * BAT_VOLT_MULTI;

  // Removed page indicator dots to prevent UI obscurity
  Serial.flush();

  switch (currentPage) {
  case 0:
    drawHome(data);
    break;
  case 1:
    drawNetwork(data);
    break;
  case 2:
    drawStatus(data);
    break;
  case 3:
    drawLog(data);
    break;
  }
  Heltec.display->display();
}

void DisplayManager::drawHome(DataManager &data) {
  // Unified Header (Dark Workspace Style)
  Heltec.display->fillRect(0, 0, 128, 13);
  Heltec.display->setColor(BLACK);
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(2, 1, "LORALINK");

  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(126, 1, FIRMWARE_VERSION);
  
  Heltec.display->setColor(WHITE);
  
  // Center Dashboard (Node ID & Mode)
  Heltec.display->setFont(ArialMT_Plain_16);
  Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
  Heltec.display->drawString(64, 25, data.myId);
  
  Heltec.display->setFont(ArialMT_Plain_10);
  String modeStr = PowerManager::isPowered() ? "PWR" : "BAT";
  if (PowerManager::getInstance().getCurrentMode() != PowerMode::NORMAL) modeStr = "CNSRV";
  Heltec.display->drawString(64, 40, modeStr);

  // Bottom Gauges
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(0, 52, "B:" + String(batteryVolts, 2) + "V");
  
  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  int nodes = data.numNodes;
  Heltec.display->drawString(128, 52, "NODES:" + String(nodes));
}

void DisplayManager::drawNetwork(DataManager &data) {
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(0, 0, "NETWORK");
  Heltec.display->drawLine(0, 12, 128, 12);

#ifdef SUPPORT_WIFI
  if (WiFi.status() == WL_CONNECTED) {
    Heltec.display->setFont(ArialMT_Plain_16);
    Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
    Heltec.display->drawString(64, 14, WiFi.localIP().toString());
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->drawString(64, 33, "SSID: " + WiFi.SSID());

    // Show first peer's resolved IP if available
    if (data.numNodes > 0 && data.remoteNodes[0].ip[0] != '\0') {
      Heltec.display->drawString(64, 45,
        String(data.remoteNodes[0].id) + ": " + String(data.remoteNodes[0].ip));
    } else {
      Heltec.display->drawString(64, 45, String(WiFi.RSSI()) + " dBm");
    }
#else
  if (false) {
#endif
    Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
    Heltec.display->drawString(128, 0, "[" + data.getMacSuffix() + "]");
  } else {
    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->setTextAlignment(TEXT_ALIGN_CENTER);
    if (data.wifiSsid.length() > 0) {
      Heltec.display->drawString(64, 20, "Connecting to:");
      Heltec.display->setFont(ArialMT_Plain_16);
      Heltec.display->drawString(64, 35, data.wifiSsid);
    } else {
      Heltec.display->setFont(ArialMT_Plain_16);
      Heltec.display->drawString(64, 22, "No WiFi");
      Heltec.display->setFont(ArialMT_Plain_10);
      Heltec.display->drawString(64, 42, "Use Web Config");
    }
  }
  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->drawString(128, 0, "[" + data.getMacSuffix() + "]");
}

void DisplayManager::drawStatus(DataManager &data) {
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(0, 0, "SYSTEM STATUS");
  Heltec.display->drawLine(0, 12, 128, 12);

  unsigned long s = millis() / 1000;
  String uptime = String(s / 3600) + "h " + String((s % 3600) / 60) + "m";

  // Tiny Gauging Layout
  Heltec.display->drawString(0, 16, "BATTERY: " + String(batteryVolts, 2) + "V");
  Heltec.display->drawString(0, 26, "UPTIME:  " + uptime);
  Heltec.display->drawString(0, 36, "NODES:   " + String(data.numNodes));
  Heltec.display->drawString(0, 46, "CORE:    " + String(ESP.getFreeHeap() / 1024) + "KB");
  
  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->drawString(128, 52, FIRMWARE_VERSION);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void DisplayManager::drawLog(DataManager &data) {
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->drawString(0, 0, "LOG");
  Heltec.display->drawLine(0, 12, 128, 12);

  for (int i = 0; i < 4; i++) {
    int idx = (data.logIndex - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (strlen(data.msgLog[idx].message) > 0) {
      String msg = String(data.msgLog[idx].message);
      Heltec.display->drawString(0, 14 + i * 12, msg.substring(0, 21));
    }
  }
  Heltec.display->setTextAlignment(TEXT_ALIGN_RIGHT);
  Heltec.display->drawString(128, 0, "[" + data.getMacSuffix() + "]");
}
