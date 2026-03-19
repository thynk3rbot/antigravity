#pragma once
#include <Arduino.h>
#include <functional>

enum class PowerMode : uint8_t {
    NORMAL   = 0,  // Full operation, all features enabled
    CONSERVE = 1,  // Reduced transmit power, longer sleep intervals
    CRITICAL = 2   // Minimal operation, display off, max sleep
};

class PowerManager {
public:
    // Callback type for power mode changes
    typedef std::function<void(PowerMode newMode)> PowerModeCallback;

    static void begin();

    // Legacy init (alias for begin, returns true always)
    static bool init();

    // VEXT control
    static void enableVEXT();
    static void disableVEXT();

    // Battery monitoring
    static float getBatteryVoltage();   // Returns voltage (e.g. 3.7)
    static uint8_t getBatteryPercent(); // Returns 0-100

    // Power mode management
    static PowerMode getMode();
    static void setMode(PowerMode mode);
    static void autoUpdateMode();  // Call periodically - auto-sets mode based on voltage

    // Mode-dependent intervals (used by heartbeat, mesh aging, etc.)
    static uint32_t getHeartbeatIntervalMs();  // NORMAL=30s, CONSERVE=60s, CRITICAL=120s
    static uint32_t getSleepIntervalMs();      // NORMAL=0 (no sleep), CONSERVE=5s, CRITICAL=30s

    // Legacy compatibility methods (used by main.cpp)
    static void update();                           // Calls autoUpdateMode() + fires callback on change
    static void onModeChange(PowerModeCallback cb); // Register callback for mode transitions
    static void printStatus();                      // Print status to Serial

private:
    static PowerMode _mode;
    static float _lastVoltage;
    static PowerModeCallback _modeChangeCallback;

    // Voltage thresholds
    static constexpr float VOLT_NORMAL   = 3.7f;  // Above this = NORMAL
    static constexpr float VOLT_CONSERVE = 3.4f;  // Above this = CONSERVE
    // Below VOLT_CONSERVE = CRITICAL

    // ADC conversion constants (calibrate for ESP32 ADC)
    static constexpr float ADC_VREF     = 3.3f;
    static constexpr float ADC_MAX      = 4095.0f;
    static constexpr float VDIV_RATIO   = 2.0f;   // Voltage divider on battery pin
};
