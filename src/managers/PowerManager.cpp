#include "PowerManager.h"
#include "../utils/DebugMacros.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "WiFiManager.h"

PowerManager::PowerManager() {
  _currentMode = PowerMode::NORMAL;
  _manualOverride = false;
  _lastVoltage = 4.2f;
  _lastSampleMs = 0;
}

void PowerManager::Init() {
  LOG_PRINTLN("POWER: Miser initializing...");
  pinMode(PIN_BAT_ADC, INPUT);

#if defined(ARDUINO_ARCH_ESP32)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // 0 - 3.1V range
#endif

  // Heltec V3 External Power Control
  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);

  // Heltec V3 Battery Divider Control
  pinMode(PIN_BAT_CTRL, OUTPUT);
  digitalWrite(PIN_BAT_CTRL, LOW);

  Update();
}

float PowerManager::getBatteryVoltage() {
  uint32_t raw = analogRead(PIN_BAT_ADC);
  float volt = (raw / 4095.0f) * 3.3f * BAT_VOLT_MULTI;
  _lastVoltage = volt;
  LOG_PRINTF("POWER: Battery ADC=%u, V=%.2fV (Multi=%.2f)\n", raw, volt,
             (float)BAT_VOLT_MULTI);
  return volt;
}

void PowerManager::Update() {
  unsigned long now = millis();
  if (now - _lastSampleMs > 30000 || _lastSampleMs == 0) { // Every 30s
    _lastSampleMs = now;
    float v = getBatteryVoltage();
    evaluateMode();
    LOG_PRINTF("POWER: Miser V=%.2fV Mode=%s\n", v, getModeString().c_str());
  }
}

void PowerManager::evaluateMode() {
  if (_manualOverride)
    return;

  PowerMode prev = _currentMode;

  // USB/Mains detection: If voltage is < 3.0V (Heltec V3 divider often floats
  // at 2.8V on USB) or > 4.4V (USB 5V Rail), force NORMAL mode.
  if (_lastVoltage < 3.0f || _lastVoltage > 4.4f) {
    if (_currentMode != PowerMode::NORMAL) {
      _currentMode = PowerMode::NORMAL;
      LOG_PRINTLN("POWER: Miser -> NORMAL (USB Detection)");
    }
  } else if (_lastVoltage >= POWER_MISER_VOLT_NORMAL) {
    if (_currentMode != PowerMode::NORMAL) {
      _currentMode = PowerMode::NORMAL;
      LOG_PRINTLN("POWER: Miser -> NORMAL (Battery High)");
    }
  } else if (_lastVoltage >= POWER_MISER_VOLT_CONSERVE) {
    if (_currentMode != PowerMode::CONSERVE) {
      _currentMode = PowerMode::CONSERVE;
      LOG_PRINTLN("POWER: Miser -> CONSERVE (Battery Low)");
    }
  } else {
    if (_currentMode != PowerMode::CRITICAL) {
      _currentMode = PowerMode::CRITICAL;
      LOG_PRINTLN("POWER: Miser -> CRITICAL (Battery Empty)");
    }
  }

  if (prev != _currentMode) {
    LOG_PRINTF("POWER: Miser switched to %s\n", getModeString().c_str());

    // Apply instant policy changes
    if (_currentMode == PowerMode::CRITICAL) {
      DisplayManager::getInstance().SetDisplayActive(false);
    }

    // Notify WiFi Manager for immediate radio coordination
    WiFiManager::getInstance().onPowerStateChange(_currentMode);
  }
}

PowerMode PowerManager::getCurrentMode() { return _currentMode; }

uint32_t PowerManager::getTargetInterval() {
  switch (_currentMode) {
  case PowerMode::NORMAL:
    return POWER_MISER_HB_NORMAL;
  case PowerMode::CONSERVE:
    return POWER_MISER_HB_CONSERVE;
  case PowerMode::CRITICAL:
    return POWER_MISER_HB_CRITICAL;
  default:
    return POWER_MISER_HB_NORMAL;
  }
}

bool PowerManager::isOledAllowed() {
  return (_currentMode != PowerMode::CRITICAL);
}

bool PowerManager::isWifiAllowed() {
  return (_currentMode != PowerMode::CRITICAL);
}

String PowerManager::getModeString() {
  switch (_currentMode) {
  case PowerMode::NORMAL:
    return "NORMAL";
  case PowerMode::CONSERVE:
    return "CONSERVE";
  case PowerMode::CRITICAL:
    return "CRITICAL";
  default:
    return "UNKNOWN";
  }
}

void PowerManager::setManualMode(PowerMode mode, bool manual) {
  _manualOverride = manual;
  if (manual)
    _currentMode = mode;
}
