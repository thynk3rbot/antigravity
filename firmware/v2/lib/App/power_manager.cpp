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

#ifdef HAS_PMIC
XPowersPMIC                      PowerManager::_pmic;
#endif

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
#if defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(ARDUINO_HELTEC_WIFI_LORA_32_V3) || defined(ARDUINO_HELTEC_WIFI_LORA_32)
    digitalWrite(VEXT_PIN, HIGH);  // Heltec V2/V3/V4 all use Active LOW -> Start OFF
#else
    digitalWrite(VEXT_PIN, LOW);   // Default assumes Active HIGH
#endif
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
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    digitalWrite(BAT_ADC_CTRL, HIGH);   // On V4, HIGH enables sense
#else
    digitalWrite(BAT_ADC_CTRL, LOW);    // On V2/V3, LOW typically enables sense
#endif
#endif

#ifdef HAS_PMIC
    Wire.begin(I2C_SDA, I2C_SCL);
    if (_pmic.init(Wire, I2C_SDA, I2C_SCL, AXP192_SLAVE_ADDRESS)) {
        Serial.println("[PMIC] AXP192 Initialized Successfully!");
        _pmic.setChargeControlCur(300); // 300mA charge
        _pmic.enableVbusVoltageMeasure();
        _pmic.enableBattVoltageMeasure();
        _pmic.enableSystemVoltageMeasure();
        
        // Turn on GPS Power Rail (LDO3 on T-Beam V1.1 is GPS, typically 3.3V)
        _pmic.setLDO3Voltage(3300);
        _pmic.enableLDO3();
        
        // Turn on LoRa Power Rail (LDO2 on T-Beam V1.1 is LoRa, typically 3.3V)
        _pmic.setLDO2Voltage(3300);
        _pmic.enableLDO2();
    } else {
        Serial.println("[PMIC] FAILED to initialize AXP PMIC!");
    }
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
        Serial.printf("[Power] USB detected (%.2fV)  overriding %s  NORMAL\n",
                      _lastVoltage, _mode == PowerMode::CRITICAL ? "CRITICAL" : "CONSERVE");
        _mode = PowerMode::NORMAL;
        NVSManager::setPowerMode(static_cast<uint8_t>(PowerMode::NORMAL));
    }
    enableVEXT();  // Ensure peripherals like OLED are powered on boot
    
    // On V2 (pulsing mode), wait for stability before proceeding to init HAL/Transports
    uint8_t timeout = 0;
    while (!isVEXTStable() && timeout < 20) {
        delay(10);
        timeout++;
    }
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
    Serial.printf("[PWR] Enabling VEXT on pin %d (HW Variant Parity)\n", VEXT_PIN);
    pinMode(VEXT_PIN, OUTPUT);
    
    // Pulse sequence for rail stabilization
#if defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(ARDUINO_HELTEC_WIFI_LORA_32_V3) || defined(ARDUINO_HELTEC_WIFI_LORA_32)
    digitalWrite(VEXT_PIN, HIGH);  delay(100);
    digitalWrite(VEXT_PIN, LOW);   delay(100);
    digitalWrite(VEXT_PIN, LOW);   // Heltec Active LOW -> Leave ON
#else
    digitalWrite(VEXT_PIN, LOW);   delay(100);
    digitalWrite(VEXT_PIN, HIGH);  delay(100);
    digitalWrite(VEXT_PIN, HIGH);  // Default Active HIGH -> Leave ON
#endif
    
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    pinMode(37, OUTPUT);
    digitalWrite(37, HIGH);        // V4 Battery Sense Enable
#endif

    _vextPulseState = 0;           
    delay(200);                    // Stabilization delay before I2C initialization
#endif
}

void PowerManager::disableVEXT() {
#ifdef VEXT_PIN
#if defined(ARDUINO_HELTEC_WIFI_LORA_32_V4) || defined(ARDUINO_HELTEC_WIFI_LORA_32_V3) || defined(ARDUINO_HELTEC_WIFI_LORA_32)
    digitalWrite(VEXT_PIN, HIGH);  // Heltec Active LOW -> OFF
#else
    digitalWrite(VEXT_PIN, LOW);   // Default assumes Active HIGH
#endif
#endif
}

// ============================================================================
// Battery monitoring
// ============================================================================

float PowerManager::getBatteryVoltage() {
#ifdef HAS_PMIC
    float voltage = _pmic.getBattVoltage() / 1000.0f; // mV to V
    _lastVoltage = voltage;
    return voltage;
#elif defined(BAT_ADC_PIN)
#ifdef BAT_ADC_CTRL
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    digitalWrite(BAT_ADC_CTRL, HIGH); // Enable
#else
    digitalWrite(BAT_ADC_CTRL, LOW);  // Enable 
#endif
    delay(5);
#endif

#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    // Use factory-calibrated millivolts reading for S3-V4 accuracy
    float readingRaw = static_cast<float>(analogReadMilliVolts(BAT_ADC_PIN));
    float voltage = (readingRaw / 1000.0f) * BAT_ADC_VOLTAGE_DIVIDER; 
#else
    // Average 5 readings for stability on legacy ESP32
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 5; i++) {
        sum += analogRead(BAT_ADC_PIN);
    }
    float readingAvg = static_cast<float>(sum) / 5.0f;
    float voltage = (readingAvg / 4095.0f) * 3.3f * BAT_ADC_VOLTAGE_DIVIDER; 
#endif

#ifdef BAT_ADC_CTRL
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    digitalWrite(BAT_ADC_CTRL, LOW); // Disable
#else
    digitalWrite(BAT_ADC_CTRL, HIGH); // Disable
#endif
#endif
    _lastVoltage = voltage;
    return voltage;
#else
    // No ADC pin defined  return fake full battery
    return 4.2f;
#endif
}

uint8_t PowerManager::getBatteryPercent() {
#ifdef HAS_PMIC
    return _pmic.getBatteryPercent();
#else
    float voltage = getBatteryVoltage();
    
    // Non-linear Li-Ion discharge curve (Standard 3.7V Cell)
    // 4.2V = 100%, 3.7V = 50%, 3.0V = 0%
    if (voltage >= 4.20f) return 100;
    if (voltage >= 4.15f) return 98;
    if (voltage >= 4.10f) return 95;
    if (voltage >= 4.05f) return 90;
    if (voltage >= 3.95f) return 80;
    if (voltage >= 3.85f) return 60;
    if (voltage >= 3.75f) return 40;
    if (voltage >= 3.70f) return 20;
    if (voltage >= 3.60f) return 10;
    if (voltage >= 3.50f) return 5;
    if (voltage < 3.00f)  return 0;

    // Linear fallback for middle ranges to avoid stair-stepping
    float percent = (voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    if (percent > 100.0f) percent = 100.0f;
    if (percent < 0.0f) percent = 0.0f;
    return static_cast<uint8_t>(percent);
#endif
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
        // USB/External power detected - Always force NORMAL mode
        if (_mode != PowerMode::NORMAL) {
            Serial.printf("[Power] USB power detected (%.2fV)  restoring NORMAL mode\n", _lastVoltage);
        }
        newMode = PowerMode::NORMAL;
    } else if (voltage >= VOLT_NORMAL + (_mode > PowerMode::NORMAL ? hysteresis : 0)) {
        newMode = PowerMode::NORMAL;
    } else if (voltage >= VOLT_CONSERVE + (_mode > PowerMode::CONSERVE ? hysteresis : 0)) {
        newMode = PowerMode::CONSERVE;
    } else {
        newMode = PowerMode::CRITICAL;
        if (_mode != PowerMode::CRITICAL) {
             Serial.printf("[Power] Low Battery (%.2fV)  entering CRITICAL mode\n", _lastVoltage);
        }
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
        default:                  return 0;      // NORMAL  no sleep
    }
}

bool PowerManager::isPowered() {
#ifdef HAS_PMIC
    return _pmic.isVbusIn();
#else
    float volt = _lastVoltage;
    // USB/Mains: battery reads near 0V-0.8V (no battery) or > 4.25V (charging/bus)
    // LiPo cells should never be below 2.5V; anything under 1.5V is considered "USB only"
    return (volt < 1.5f || volt > 4.25f);
#endif
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
    Serial.printf("Heartbeat:         %u ms\n", (unsigned int)getHeartbeatIntervalMs());
    Serial.printf("Sleep Interval:    %u ms\n", (unsigned int)getSleepIntervalMs());
    Serial.printf("USB Powered:       %s\n", isPowered() ? "Yes" : "No");
    Serial.println("=========================================\n");
}
