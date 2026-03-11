#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "../config.h"
#include <Arduino.h>

enum class PowerMode : uint8_t { NORMAL = 0, CONSERVE = 1, CRITICAL = 2 };

class PowerManager {
public:
  static PowerManager &getInstance() {
    static PowerManager instance;
    return instance;
  }

  void Init();
  void Update();

  float getBatteryVoltage();
  PowerMode getCurrentMode();
  uint32_t getTargetInterval();

  bool isOledAllowed();
  bool isWifiAllowed();
  bool isBleAllowed();

  // Returns true if running on USB/mains (battery reads near 0V or > 4.25V charging)
  static bool isPowered() {
    uint32_t raw = analogRead(PIN_BAT_ADC);
    float volt = (raw / 4095.0f) * 3.3f * BAT_VOLT_MULTI;
    return (volt < 0.1f || volt > 4.25f);
  }

  // Power-Miser API
  void setManualMode(PowerMode mode, bool manual);
  String getModeString();

  // Solar Trend Analysis API
  float getVelocityMvMin();       // mV/min (positive=charging, negative=drain)
  float getTimeToEmptyHours();    // Hours until CRITICAL at current drain rate
  bool  isTrendValid();           // True if ring buffer has enough samples

  // VEXT Peripheral Gating
  void enableVext();              // Turn on external power rail
  void disableVext();             // Turn off external power rail
  bool isVextEnabled();

private:
  PowerManager();

  PowerMode _currentMode;
  PowerMode _previousMode;        // For detecting transitions
  bool _manualOverride;
  float _lastVoltage;
  unsigned long _lastSampleMs;
  bool _vextEnabled;

  // Voltage trend ring buffer
  float _voltageRing[POWER_MISER_TREND_SAMPLES];
  unsigned long _timestampRing[POWER_MISER_TREND_SAMPLES];
  uint8_t _ringHead;
  uint8_t _ringCount;

  void evaluateMode();
  void applyModePolicy(PowerMode newMode);
  void recordSample(float voltage);
};

#endif // POWER_MANAGER_H
