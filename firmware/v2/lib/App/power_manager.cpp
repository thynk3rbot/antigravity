#include "power_manager.h"
#include "../HAL/board_config.h"
#include "nvs_manager.h"

// ============================================================================
// Static member definitions
// ============================================================================

PowerMode                        PowerManager::_mode                = PowerMode::NORMAL;
float                            PowerManager::_lastVoltage         = 4.2f;
PowerManager::PowerModeCallback  PowerManager::_modeChangeCallback  = nullptr;
void*                            PowerManager::_vextTimer           = nullptr;
uint8_t                          PowerManager::_vextPulseState       = 0; // 0 = IDLE, 1-3 = PULSING

// ============================================================================
// VEXT control timer callback
// ============================================================================

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

void PowerManager::_vextTimerCallback(TimerHandle_t xTimer) {
#ifdef VEXT_PIN
    switch (_vextPulseState) {
        case 1: // Just finished first LOW, move to HIGH
            digitalWrite(VEXT_PIN, HIGH);
            _vextPulseState = 2;
            xTimerStart(xTimer, 0);
            break;
        case 2: // Just finished HIGH, move to final LOW
            digitalWrite(VEXT_PIN, LOW);
            _vextPulseState = 3;
            xTimerStart(xTimer, 0);
            break;
        case 3: // Done
            _vextPulseState = 0; // IDLE/STABLE
            break;
    }
#endif
}

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

    // Create the pulse timer (non-blocking)
    if (!_vextTimer) {
        _vextTimer = xTimerCreate("VEXTPulse", pdMS_TO_TICKS(50), pdFALSE, nullptr, (TimerCallbackFunction_t)_vextTimerCallback);
    }

#ifdef BAT_ADC_PIN
    pinMode(BAT_ADC_PIN, INPUT);
#endif
#ifdef BAT_ADC_CTRL
    pinMode(BAT_ADC_CTRL, OUTPUT);
    digitalWrite(BAT_ADC_CTRL, LOW);   // Disable by default (high power)
#endif

    // Restore persisted power mode from NVS
    uint8_t saved = NVSManager::getPowerMode(0);
    if (saved <= static_cast<uint8_t>(PowerMode::CRITICAL)) {
        _mode = static_cast<PowerMode>(saved);
    } else {
        _mode = PowerMode::NORMAL;
    }
}

bool PowerManager::init() {
    begin();
    // Prime the ADC reading so isPowered() works immediately
    _lastVoltage = getBatteryVoltage();
    // If USB powered, override any persisted CRITICAL/CONSERVE mode
    if (isPowered() && _mode != PowerMode::NORMAL) {
        Serial.printf("[Power] USB detected (%.2fV) — overriding %s → NORMAL\n",
                      _lastVoltage, _mode == PowerMode::CRITICAL ? "CRITICAL" : "CONSERVE");
        _mode = PowerMode::NORMAL;
        NVSManager::setPowerMode(static_cast<uint8_t>(PowerMode::NORMAL));
    }
    enableVEXT();  // Ensure peripherals like OLED are powered on boot
    return true;
}

// ============================================================================
// VEXT control
// ============================================================================

bool PowerManager::isVEXTStable() {
    return (_vextPulseState == 0);
}

void PowerManager::enableVEXT() {
#ifdef VEXT_PIN
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
    if (_vextPulseState != 0) return; // Already pulsing
    
    // Pulse VEXT for V2 display stability (Non-blocking Timer Pattern)
    digitalWrite(VEXT_PIN, LOW);
    _vextPulseState = 1;
    if (_vextTimer) xTimerStart((TimerHandle_t)_vextTimer, 0);
#else
    digitalWrite(VEXT_PIN, LOW);   // LOW = power ON on Heltec boards
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    pinMode(37, OUTPUT);
    digitalWrite(37, HIGH);        // V4 Power Control (Active HIGH)
#endif
    _vextPulseState = 0; // Immediately stable
#endif
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
#ifdef BAT_ADC_CTRL
    digitalWrite(BAT_ADC_CTRL, LOW);  // Enable voltage divider (Active LOW on Heltec)
    delay(5);
#endif
    // Average 5 readings for stability
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 5; i++) {
        sum += analogRead(BAT_ADC_PIN);
    }
#ifdef BAT_ADC_CTRL
    digitalWrite(BAT_ADC_CTRL, HIGH);   // Disable voltage divider (Passive HIGH)
#endif
    float reading = static_cast<float>(sum) / 5.0f;
    float voltage = (reading / 4095.0f) * 3.3f * 2.0f; // V4/V3 Standard Divider (6.6V Full Scale)
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
    NVSManager::setPowerMode(static_cast<uint8_t>(mode));
}

void PowerManager::autoUpdateMode() {
    float voltage = getBatteryVoltage();
    _lastVoltage = voltage;

    PowerMode newMode = _mode;
    float hysteresis = 0.05f; // 50mV parity with V1

    if (isPowered()) {
        newMode = PowerMode::NORMAL;
    } else if (voltage >= VOLT_NORMAL + (_mode > PowerMode::NORMAL ? hysteresis : 0)) {
        newMode = PowerMode::NORMAL;
    } else if (voltage >= VOLT_CONSERVE + (_mode > PowerMode::CONSERVE ? hysteresis : 0)) {
        newMode = PowerMode::CONSERVE;
    } else if (voltage < VOLT_CONSERVE - (newMode < PowerMode::CRITICAL ? hysteresis : 0)) {
        newMode = PowerMode::CRITICAL;
    }

    if (newMode != _mode) {
        _mode = newMode;
        NVSManager::setPowerMode(static_cast<uint8_t>(_mode));
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
    if (isPowered()) return 0;
    switch (_mode) {
        case PowerMode::CONSERVE: return 5000;
        case PowerMode::CRITICAL: return 30000;
        default:                  return 0;      // NORMAL — no sleep
    }
}

bool PowerManager::isPowered() {
    float volt = _lastVoltage;
    // USB/Mains: battery reads near 0V (no battery) or > 4.25V (charging/bus)
    // Same logic as V1 PowerManager::isPowered()
    return (volt < 0.1f || volt > 4.25f);
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
    Serial.printf("USB Powered:       %s\n", isPowered() ? "Yes" : "No");
    Serial.println("=========================================\n");
}
