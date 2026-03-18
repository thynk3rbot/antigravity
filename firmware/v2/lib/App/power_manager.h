/**
 * @file power_manager.h
 * @brief Power Management with Battery Voltage Monitoring
 *
 * Implements 3-tier power modes based on battery voltage:
 * - NORMAL (>= 3.2V): Full operation, all transports active, bright OLED
 * - CONSERVE (2.8-3.2V): Reduced activity, WiFi disabled, dim OLED, slower heartbeat
 * - CRITICAL (< 2.8V): Minimum operation, LoRa only, OLED off, sleep mode
 *
 * Battery voltage is monitored via ADC with low-pass filtering (3-sample average).
 * Mode transitions trigger registered callbacks for dependent modules.
 * VEXT rail control for external sensor/radio power management.
 */

#pragma once

#include <cstdint>
#include <functional>

/**
 * @enum PowerMode
 * @brief 3-tier power management states
 */
enum class PowerMode : uint8_t {
    NORMAL,     ///< Full operation (>= 3.2V)
    CONSERVE,   ///< Reduced activity (2.8-3.2V)
    CRITICAL    ///< Minimum operation (< 2.8V)
};

/**
 * @class PowerManager
 * @brief Singleton power management with battery voltage monitoring
 *
 * Reads battery voltage periodically, smooths with low-pass filter,
 * detects mode transitions, and triggers callbacks for dependent modules.
 */
class PowerManager {
public:
    /// Callback type for power mode changes
    typedef std::function<void(PowerMode newMode)> PowerModeCallback;

    /**
     * @brief Initialize power manager and ADC sampling
     * @return true if initialization successful, false otherwise
     */
    static bool init();

    /**
     * @brief Get current power mode
     * @return Current PowerMode (NORMAL, CONSERVE, or CRITICAL)
     */
    static PowerMode getMode();

    /**
     * @brief Get current battery voltage
     * @return Battery voltage in volts (actual cell voltage, not ADC input)
     */
    static float getBatteryVoltage();

    /**
     * @brief Get the last ADC raw reading (for diagnostics)
     * @return Raw ADC value (0-4095 for 12-bit)
     */
    static uint16_t getLastRawADC();

    /**
     * @brief Register callback for power mode changes
     * Called once per mode transition (not on every update).
     * @param callback Function to call with new mode
     */
    static void onModeChange(PowerModeCallback callback);

    /**
     * @brief Update power monitoring
     * Call periodically from main loop or FreeRTOS task (every 30-60 seconds recommended).
     * - Reads battery voltage via ADC (10-sample average)
     * - Applies low-pass filter (3-value history)
     * - Detects mode transitions
     * - Calls registered callback if mode changed
     */
    static void update();

    /**
     * @brief Enable/disable VEXT (external power rail)
     * Controls power to external sensors and radio amplifier.
     * @param enabled true to turn on VEXT, false to turn off
     */
    static void setVEXTEnabled(bool enabled);

    /**
     * @brief Get VEXT status
     * @return true if VEXT is currently enabled
     */
    static bool isVEXTEnabled();

    /**
     * @brief Get recommended heartbeat interval for current mode
     * @return Milliseconds between heartbeat packets
     *   - NORMAL: 10000 ms (10 seconds)
     *   - CONSERVE: 30000 ms (30 seconds)
     *   - CRITICAL: 60000 ms (60 seconds)
     */
    static uint16_t getHeartbeatIntervalMs();

    /**
     * @brief Check if WiFi should be active in current mode
     * @return true if WiFi is enabled, false otherwise
     *   - NORMAL: true (enabled)
     *   - CONSERVE: false (disabled)
     *   - CRITICAL: false (disabled)
     */
    static bool isWiFiEnabled();

    /**
     * @brief Check if OLED should use bright display in current mode
     * @return true for bright, false for dim/off
     *   - NORMAL: true (bright)
     *   - CONSERVE: true (bright, but reduced refresh)
     *   - CRITICAL: false (off to save power)
     */
    static bool isOLEDBright();

    /**
     * @brief Print status to Serial for diagnostics
     */
    static void printStatus();

    /**
     * @brief Set interval for battery voltage polling (for testing)
     * @param intervalMs New polling interval in milliseconds
     */
    static void setUpdateIntervalMs(uint16_t intervalMs);

private:
    // Private constructor (singleton pattern)
    PowerManager();
};
