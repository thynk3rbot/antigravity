#include "PowerManager.h"
#include "../utils/DebugMacros.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "WiFiManager.h"

PowerManager::PowerManager() {
  _currentMode = PowerMode::NORMAL;
  _previousMode = PowerMode::NORMAL;
  _manualOverride = false;
  _lastVoltage = 4.2f;
  _lastSampleMs = 0;
  _vextEnabled = false;
  _ringHead = 0;
  _ringCount = 0;
  memset(_voltageRing, 0, sizeof(_voltageRing));
  memset(_timestampRing, 0, sizeof(_timestampRing));
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
  enableVext(); // Start with peripherals ON

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
  if (now - _lastSampleMs > POWER_MISER_SAMPLE_INTERVAL_MS ||
      _lastSampleMs == 0) {
    _lastSampleMs = now;
    float v = getBatteryVoltage();
    recordSample(v);
    evaluateMode();
    LOG_PRINTF("POWER: Miser V=%.2fV Mode=%s Vel=%.1fmV/min TTE=%.1fh\n", v,
               getModeString().c_str(), getVelocityMvMin(),
               getTimeToEmptyHours());
  }
}

// ── Core Tier Intelligence (STABILITY LOCK) ─────────────────────────────────
void PowerManager::evaluateMode() {
  // Policy engine disabled until main functionality verified.
  // We keep the logic commented but force NORMAL.
  _currentMode = PowerMode::NORMAL;
  digitalWrite(PIN_VEXT_CTRL, LOW); // Force VEXT ON
}

// ── Side-effect policy engine (DEACTIVATED) ──────────────────────────────────
void PowerManager::applyModePolicy(PowerMode newMode) {
  // No-op for stability
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

bool PowerManager::isBleAllowed() {
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
  if (manual) {
    PowerMode oldMode = _currentMode;
    _currentMode = mode;
    if (oldMode != mode) {
      _previousMode = oldMode;
      applyModePolicy(mode);
    }
  }
}

// ── VEXT Peripheral Gating ──────────────────────────────────────────────────
void PowerManager::enableVext() {
  if (!_vextEnabled) {
    digitalWrite(PIN_VEXT_CTRL, LOW); // LOW = ON on Heltec V3
    _vextEnabled = true;
    LOG_PRINTLN("POWER: VEXT rail ON");
  }
}

void PowerManager::disableVext() {
  if (_vextEnabled) {
    digitalWrite(PIN_VEXT_CTRL, HIGH); // HIGH = OFF on Heltec V3
    _vextEnabled = false;
    LOG_PRINTLN("POWER: VEXT rail OFF");
  }
}

bool PowerManager::isVextEnabled() { return _vextEnabled; }

// ── Voltage Trend Ring Buffer ───────────────────────────────────────────────
void PowerManager::recordSample(float voltage) {
  _voltageRing[_ringHead] = voltage;
  _timestampRing[_ringHead] = millis();
  _ringHead = (_ringHead + 1) % POWER_MISER_TREND_SAMPLES;
  if (_ringCount < POWER_MISER_TREND_SAMPLES)
    _ringCount++;
}

bool PowerManager::isTrendValid() {
  return (_ringCount >= 3); // Need at least 3 samples for a meaningful slope
}

float PowerManager::getVelocityMvMin() {
  if (!isTrendValid())
    return 0.0f;

  // Linear regression: least-squares slope over ring buffer
  // Using oldest and newest for simple two-point slope (robust enough for 6min)
  uint8_t oldest =
      (_ringCount < POWER_MISER_TREND_SAMPLES)
          ? 0
          : _ringHead; // Oldest is at head when buffer is full
  uint8_t newest = (_ringHead == 0) ? (POWER_MISER_TREND_SAMPLES - 1)
                                    : (_ringHead - 1);

  float dV = _voltageRing[newest] - _voltageRing[oldest];
  float dT_ms =
      (float)(_timestampRing[newest] - _timestampRing[oldest]);

  if (dT_ms < 1000.0f)
    return 0.0f; // Guard against division by near-zero

  float dT_min = dT_ms / 60000.0f;
  return (dV * 1000.0f) / dT_min; // Convert V to mV, per minute
}

float PowerManager::getTimeToEmptyHours() {
  float velocity = getVelocityMvMin();
  if (velocity >= 0.0f)
    return 99.0f; // Charging or flat — report "long time"

  float headroom_mV =
      (_lastVoltage - POWER_MISER_VOLT_CRITICAL) * 1000.0f;
  if (headroom_mV <= 0.0f)
    return 0.0f; // Already at or below critical

  float drain_mV_per_hour = fabsf(velocity) * 60.0f;
  if (drain_mV_per_hour < 0.1f)
    return 99.0f; // Guard near-zero drain

  return headroom_mV / drain_mV_per_hour;
}
