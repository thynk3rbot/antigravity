/**
 * @file power_manager.cpp
 * @brief Power Management Implementation
 *
 * Implements battery voltage monitoring via ADC with 3-tier power modes.
 * Uses low-pass filter (3-sample rolling average) to smooth voltage readings.
 * Mode transitions trigger registered callbacks for dependent modules.
 */

#include "power_manager.h"
#include "../HAL/board_config.h"
#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <hal/adc_types.h>

// ============================================================================
// Static Member Variables
// ============================================================================

static PowerMode g_currentMode = PowerMode::NORMAL;
static PowerMode g_previousMode = PowerMode::NORMAL;
static float g_batteryVoltage = 3.3f;
static uint16_t g_lastRawADC = 0;
static PowerManager::PowerModeCallback g_modeChangeCallback = nullptr;
static bool g_vextEnabled = false;
static uint32_t g_lastUpdateMs = 0;
static uint16_t g_updateIntervalMs = 30000;  // 30 seconds default

// Low-pass filter: keep last 3 voltage readings for smoothing
static constexpr uint8_t FILTER_HISTORY_SIZE = 3;
static float g_voltageHistory[FILTER_HISTORY_SIZE] = {3.3f, 3.3f, 3.3f};
static uint8_t g_historyIndex = 0;

// ADC calibration data
static esp_adc_cal_characteristics_t adc1_chars;

// ============================================================================
// ADC Initialization (ESP-IDF API)
// ============================================================================

static bool initADC() {
    // Configure ADC for battery voltage measurement
    // 12-bit resolution (0-4095 range = 0-3.3V)

#if BAT_ADC_UNIT == 1
    // ADC1 configuration (V2, V3)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BAT_ADC_CHANNEL, ADC_ATTEN_DB_12);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12,
                             1100,  // V_ref for ESP32
                             &adc1_chars);
#else
    // ADC2 configuration (V4)
    adc2_config_channel_atten((adc2_channel_t)BAT_ADC_CHANNEL, ADC_ATTEN_DB_12);
    esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12,
                             1100,  // V_ref for ESP32-S3
                             &adc1_chars);
#endif

    // Configure VEXT pin as GPIO output
    gpio_pad_select_gpio((gpio_num_t)VEXT_PIN);
    gpio_set_direction((gpio_num_t)VEXT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)VEXT_PIN, 0);  // Start with VEXT disabled
    g_vextEnabled = false;

    return true;
}

// ============================================================================
// ADC Reading (with averaging and filtering)
// ============================================================================

static float readBatteryVoltage() {
    // Read ADC 10 times and average (noise reduction)
    uint32_t adcSum = 0;
    constexpr uint8_t NUM_SAMPLES = 10;

    for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
#if BAT_ADC_UNIT == 1
        uint32_t raw = adc1_get_raw(BAT_ADC_CHANNEL);
#else
        // ADC2 requires error handling
        int raw = 0;
        adc2_get_raw((adc2_channel_t)BAT_ADC_CHANNEL, ADC_WIDTH_BIT_12, &raw);
#endif
        adcSum += raw;
        delayMicroseconds(100);  // Small delay between reads
    }

    uint16_t avgRaw = adcSum / NUM_SAMPLES;
    g_lastRawADC = avgRaw;

    // Convert ADC reading to voltage
    // ADC input = Vbat / 2 (due to voltage divider)
    // So: Vbat = ADC_voltage * 2
    uint32_t adcMv = esp_adc_cal_raw_to_voltage(avgRaw, &adc1_chars);
    float adcVoltage = adcMv / 1000.0f;  // Convert mV to V
    float batVoltage = adcVoltage * BAT_ADC_VOLTAGE_DIVIDER;

    return batVoltage;
}

// ============================================================================
// Low-Pass Filter (3-sample rolling average)
// ============================================================================

static float applyLowPassFilter(float rawVoltage) {
    // Add new reading to history
    g_voltageHistory[g_historyIndex] = rawVoltage;
    g_historyIndex = (g_historyIndex + 1) % FILTER_HISTORY_SIZE;

    // Calculate average
    float sum = 0.0f;
    for (uint8_t i = 0; i < FILTER_HISTORY_SIZE; i++) {
        sum += g_voltageHistory[i];
    }

    return sum / FILTER_HISTORY_SIZE;
}

// ============================================================================
// Mode Determination and Transition Detection
// ============================================================================

static PowerMode determinePowerMode(float voltage) {
    if (voltage >= BAT_VOLTAGE_NORMAL_MIN) {
        return PowerMode::NORMAL;
    } else if (voltage >= BAT_VOLTAGE_CONSERVE_MIN) {
        return PowerMode::CONSERVE;
    } else {
        return PowerMode::CRITICAL;
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool PowerManager::init() {
    if (!initADC()) {
        Serial.println("[PowerMgr] ADC init failed");
        return false;
    }

    // Take initial reading
    g_batteryVoltage = readBatteryVoltage();
    g_batteryVoltage = applyLowPassFilter(g_batteryVoltage);
    g_currentMode = determinePowerMode(g_batteryVoltage);
    g_previousMode = g_currentMode;
    g_lastUpdateMs = millis();

    Serial.printf("[PowerMgr] Initialized: Vbat=%.2fV, Mode=%u, VEXT=%s\n",
                  g_batteryVoltage,
                  static_cast<uint8_t>(g_currentMode),
                  g_vextEnabled ? "ON" : "OFF");

    return true;
}

PowerMode PowerManager::getMode() {
    return g_currentMode;
}

float PowerManager::getBatteryVoltage() {
    return g_batteryVoltage;
}

uint16_t PowerManager::getLastRawADC() {
    return g_lastRawADC;
}

void PowerManager::onModeChange(PowerModeCallback callback) {
    g_modeChangeCallback = callback;
}

void PowerManager::update() {
    uint32_t now = millis();

    // Check if it's time to update (based on configured interval)
    if (now - g_lastUpdateMs < g_updateIntervalMs) {
        return;
    }

    g_lastUpdateMs = now;

    // Read battery voltage
    float rawVoltage = readBatteryVoltage();

    // Apply low-pass filter
    g_batteryVoltage = applyLowPassFilter(rawVoltage);

    // Determine new mode
    PowerMode newMode = determinePowerMode(g_batteryVoltage);

    // Detect transition
    if (newMode != g_currentMode) {
        g_previousMode = g_currentMode;
        g_currentMode = newMode;

        // Log transition
        const char* modeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};
        Serial.printf("[PowerMgr] Mode transition: %s -> %s (Vbat=%.2fV, Raw=%u)\n",
                      modeNames[static_cast<uint8_t>(g_previousMode)],
                      modeNames[static_cast<uint8_t>(g_currentMode)],
                      g_batteryVoltage,
                      g_lastRawADC);

        // Trigger callback if registered
        if (g_modeChangeCallback) {
            g_modeChangeCallback(g_currentMode);
        }
    }
}

void PowerManager::setVEXTEnabled(bool enabled) {
    gpio_set_level((gpio_num_t)VEXT_PIN, enabled ? 1 : 0);
    g_vextEnabled = enabled;
    Serial.printf("[PowerMgr] VEXT set to %s\n", enabled ? "ON" : "OFF");
}

bool PowerManager::isVEXTEnabled() {
    return g_vextEnabled;
}

uint16_t PowerManager::getHeartbeatIntervalMs() {
    switch (g_currentMode) {
        case PowerMode::NORMAL:
            return 10000;  // 10 seconds
        case PowerMode::CONSERVE:
            return 30000;  // 30 seconds
        case PowerMode::CRITICAL:
            return 60000;  // 60 seconds
        default:
            return 10000;
    }
}

bool PowerManager::isWiFiEnabled() {
    switch (g_currentMode) {
        case PowerMode::NORMAL:
            return true;
        case PowerMode::CONSERVE:
        case PowerMode::CRITICAL:
            return false;
        default:
            return true;
    }
}

bool PowerManager::isOLEDBright() {
    switch (g_currentMode) {
        case PowerMode::NORMAL:
        case PowerMode::CONSERVE:
            return true;
        case PowerMode::CRITICAL:
            return false;  // OLED off in critical mode
        default:
            return true;
    }
}

void PowerManager::printStatus() {
    const char* modeNames[] = {"NORMAL", "CONSERVE", "CRITICAL"};

    Serial.println("\n========== PowerManager Status ==========");
    Serial.printf("Current Mode:          %s\n", modeNames[static_cast<uint8_t>(g_currentMode)]);
    Serial.printf("Battery Voltage:       %.2f V\n", g_batteryVoltage);
    Serial.printf("Last Raw ADC:          %u (0-4095)\n", g_lastRawADC);
    Serial.printf("VEXT:                  %s\n", g_vextEnabled ? "ON" : "OFF");
    Serial.printf("WiFi Enabled:          %s\n", isWiFiEnabled() ? "Yes" : "No");
    Serial.printf("OLED Bright:           %s\n", isOLEDBright() ? "Yes" : "No");
    Serial.printf("Heartbeat Interval:    %u ms\n", getHeartbeatIntervalMs());
    Serial.printf("Update Interval:       %u ms\n", g_updateIntervalMs);
    Serial.printf("Last Update:           %lu ms ago\n", millis() - g_lastUpdateMs);
    Serial.println("=========================================\n");
}

void PowerManager::setUpdateIntervalMs(uint16_t intervalMs) {
    g_updateIntervalMs = intervalMs;
    Serial.printf("[PowerMgr] Update interval set to %u ms\n", intervalMs);
}
