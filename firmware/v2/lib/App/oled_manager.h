/**
 * @file oled_manager.h
 * @brief OLED Display Manager with 4-Page Rotating UI
 *
 * Manages a 128x64 SSD1306 OLED display showing device status across 4 pages:
 * - Page 1: Network (IP, peer count)
 * - Page 2: Power & LoRa (battery, signal strength)
 * - Page 3: Transport status (WiFi, BLE, MQTT, LoRa)
 * - Page 4: Relay, temperature, uptime, heap usage
 *
 * Features:
 * - Button control (GPIO 0): short press = next page, long press = toggle brightness
 * - Auto-rotate every 5 seconds (resets on manual page change)
 * - Periodic display updates (every 500ms)
 * - Caches frequently-accessed values for efficient rendering
 * - Power-aware brightness control via PowerManager integration
 */

#pragma once

#include <cstdint>
#include <cstring>

/**
 * @class OLEDManager
 * @brief Singleton OLED display manager with rotational UI
 *
 * Provides display initialization, periodic updates, page management,
 * and button-based control (via interrupt handler).
 */
class OLEDManager {
public:
    /**
     * @brief Initialize OLED display and button interrupt
     * @return true if initialization successful, false otherwise
     */
    static bool init();

    /**
     * @brief Refresh display (call periodically, e.g., every 500ms)
     * Should be called from the control task at regular intervals.
     * Handles page updates, auto-rotation, and time-based refreshes.
     */
    static void update();

    /**
     * @brief Draw a boot progress bar on the splash screen
     * @param label Current status label
     * @param percent 0-100 progress
     */
    static void drawBootProgress(const char* label, int percent);

    /**
     * @brief Manually change to specific page (0-3)
     * @param pageNum Page number (0-3, others are ignored)
     * Resets the auto-rotate timer.
     */
    static void setPage(uint8_t pageNum);

    /**
     * @brief Get current page number
     * @return Current page (0-3)
     */
    static uint8_t getCurrentPage();

    /**
     * @brief Set brightness level
     * @param brightness Brightness (0-255, 127 = normal, 255 = max)
     * Takes effect on next update() call.
     */
    static void setBrightness(uint8_t brightness);

    /**
     * @brief Get current brightness level
     * @return Current brightness (0-255)
     */
    static uint8_t getBrightness();

    /**
     * @brief Force OLED on/off (for power management)
     * @param on true to turn on, false to turn off
     */
    static void setDisplayOn(bool on);

    /**
     * @brief Check if OLED is currently on
     * @return true if display is enabled, false otherwise
     */
    static bool isDisplayOn();

    /**
     * @brief Update cached network IP address
     * @param ip Null-terminated string (e.g., "192.168.1.100")
     */
    static void setIP(const char* ip);

    /**
     * @brief Update cached battery voltage and power mode
     * @param voltage Battery voltage (in volts, e.g., 3.25)
     * @param mode Mode string ("NORMAL", "CONSERVE", "CRITICAL")
     */
    static void setBatteryVoltage(float voltage, const char* mode);

    /**
     * @brief Update cached LoRa signal strength
     * @param rssi RSSI value (in dBm, negative, e.g., -95)
     * @param snr SNR value (in dB, e.g., 10)
     */
    static void setLoRaSignal(int8_t rssi, int8_t snr);

    /**
     * @brief Update cached transport status
     * @param wifi WiFi enabled/connected
     * @param ble BLE enabled/connected
     * @param mqtt MQTT enabled/connected
     * @param lora LoRa enabled/connected
     */
    static void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora);

    /**
     * @brief Update cached transport status including ESP-NOW
     */
    static void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora, bool espnow);

    /**
     * @brief Update cached relay status
     * @param relayOn true if main relay is ON, false otherwise
     */
    static void setRelayStatus(bool relayOn);

    /**
     * @brief Update cached temperature
     * @param tempC Temperature in Celsius (e.g., 24.5)
     */
    static void setTemperature(float tempC);

    /**
     * @brief Update cached peer/node count
     * @param count Number of peers/nodes (0-255)
     */
    static void setPeerCount(uint8_t count);

    /**
     * @brief Update cached uptime
     * @param uptimeMs Uptime in milliseconds
     */
    static void setUptime(uint32_t uptimeMs);

    /**
     * @brief Update cached free heap size
     * @param heapBytes Free heap size in bytes
     */
    static void setFreeHeap(uint32_t heapBytes);

    /**
     * @brief Update cached GPS data
     * @param lat Latitude
     * @param lon Longitude
     * @param sats Satellite count
     * @param hasFix True if GPS has a valid lock
     */
    static void setGPS(double lat, double lon, uint8_t sats, bool hasFix);

    /**
     * @brief Update cached diagnostics
     * @param bootCount Current boot count
     * @param reason Last reset reason (e.g., "Power On", "Software")
     */
    static void setDiagnostics(uint32_t bootCount, const char* reason);

    /**
     * @brief Update cached MAC suffix
     * @param mac Null-terminated hex string (e.g., "[A1:B2]")
     */
    static void setMAC(const char* mac);

    /**
     * @brief Update cached device name
     * @param name User-set name or generated node ID
     */
    static void setDeviceName(const char* name);

    /**
     * @brief Update cached firmware version
     * @param ver Semantic version string (e.g., "0.2.2")
     */
    static void setVersion(const char* ver);

    /**
     * @brief Add a system log message to the log page
     * @param msg Message to add (will be truncated to fit)
     */
    static void addLog(const char* msg);

    /**
     * @brief Print OLED manager status to Serial (diagnostics)
     */
    static void printStatus();

private:
    // Private constructor (singleton pattern)
    OLEDManager();
};
