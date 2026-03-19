#include "power_manager.h"
#include "../HAL/board_config.h"
#include "nvs_config.h"

// ============================================================================
// Static member definitions
// ============================================================================

PowerMode                        PowerManager::_mode                = PowerMode::NORMAL;
float                            PowerManager::_lastVoltage         = 4.2f;
PowerManager::PowerModeCallback  PowerManager::_modeChangeCallback  = nullptr;

// ============================================================================
// begin() / init()
// ============================================================================

void PowerManager::begin() {
    // 12-bit ADC resolution (0-4095)
    analogReadResolution(12);

#ifdef VEXT_PIN
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, HIGH);  // HIGH = off on Heltec boards (start disabled)
#endif

#ifdef BAT_ADC_PIN
    pinMode(BAT_ADC_PIN, INPUT);
#endif

    // Restore persisted power mode from NVS
    uint8_t saved = NVSConfig::getPowerMode();
    if (saved <= static_cast<uint8_t>(PowerMode::CRITICAL)) {
        _mode = static_cast<PowerMode>(saved);
    } else {
        _mode = PowerMode::NORMAL;
    }
}

bool PowerManager::init() {
    begin();
    return true;
}

// ============================================================================
// VEXT control
// ============================================================================

void PowerManager::enableVEXT() {
#ifdef VEXT_PIN
    digitalWrite(VEXT_PIN, LOW);   // LOW = power ON on Heltec boards
#endif
}

void PowerManager::disableVEXT() {
#ifdef VEXT_PIN
    digitalWrite(VEXT_PIN, HIGH);  // HIGH = power OFF on Heltec boards
#endif
}

// ============================================================================
// Battery monitoring
// ============================================================================

float PowerManager::getBatteryVoltage() {
#ifdef BAT_ADC_PIN
    // Average 5 readings for stability
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 5; i++) {
        sum += analogRead(BAT_ADC_PIN);
    }
    float reading = static_cast<float>(sum) / 5.0f;
    float voltage = (reading / ADC_MAX) * ADC_VREF * VDIV_RATIO;
    _lastVoltage = voltage;
    return voltage;
#else
    // No ADC pin defined — return fake full battery
    return 4.2f;
#endif
}

uint8_t PowerManager::getBatteryPercent() {
    // Map voltage 3.0V -> 0%, 4.2V -> 100%
    constexpr float V_MIN = 3.0f;
    constexpr float V_MAX = 4.2f;

    float voltage = getBatteryVoltage();
    float percent = (voltage - V_MIN) / (V_MAX - V_MIN) * 100.0f;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    return static_cast<uint8_t>(percent);
}

// ============================================================================
// Power mode management
// ============================================================================

PowerMode PowerManager::getMode() {
    return _mode;
}

void PowerManager::setMode(PowerMode mode) {
    _mode = mode;
    NVSConfig::setPowerMode(static_cast<uint8_t>(mode));
}

void PowerManager::autoUpdateMode() {
    float voltage = getBatteryVoltage();
    _lastVoltage = voltage;

    PowerMode newMode;
    if (voltage >= VOLT_NORMAL) {
        newMode = PowerMode::NORMAL;
    } else if (voltage >= VOLT_CONSERVE) {
        newMode = PowerMode::CONSERVE;
    } else {
        newMode = PowerMode::CRITICAL;
    }

    if (newMode != _mode) {
        _mode = newMode;
        NVSConfig::setPowerMode(static_cast<uint8_t>(_mode));
        if (_modeChangeCallback) {
            _modeChangeCallback(_mode);
        }
    }
}

// ============================================================================
// Mode-dependent intervals
// ============================================================================

uint32_t PowerManager::getHeartbeatIntervalMs() {
    switch (_mode) {
        case PowerMode::CONSERVE: return 60000;
        case PowerMode::CRITICAL: return 120000;
        default:                  return 30000;  // NORMAL
    }
}

uint32_t PowerManager::getSleepIntervalMs() {
    switch (_mode) {
        case PowerMode::CONSERVE: return 5000;
        case PowerMode::CRITICAL: return 30000;
        default:                  return 0;      // NORMAL — no sleep
    }
}

// ============================================================================
// Legacy compatibility methods (called from main.cpp)
// ============================================================================

void PowerManager::update() {
    autoUpdateMode();
}

void PowerManager::onModeChange(PowerModeCallback cb) {
    _modeChangeCallback = cb;
}

void PowerManager::printStatus() {
    const char* modeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};
    Serial.println("\n========== PowerManager Status ==========");
    Serial.printf("Current Mode:      %s\n", modeNames[static_cast<uint8_t>(_mode)]);
    Serial.printf("Battery Voltage:   %.2f V\n", _lastVoltage);
    Serial.printf("Battery Percent:   %u%%\n", getBatteryPercent());
    Serial.printf("Heartbeat:         %lu ms\n", getHeartbeatIntervalMs());
    Serial.printf("Sleep Interval:    %lu ms\n", getSleepIntervalMs());
    Serial.println("=========================================\n");
}
