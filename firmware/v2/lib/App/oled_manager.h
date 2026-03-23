/**
 * @file oled_manager.h
 * @brief OLED Display Manager with 4-Page Rotating UI
 *
 * Manages a 128x64 SSD1306 OLED display showing device status across 4 pages:
 * - Page 1: Network (IP, peer count)
 * - Page 2: Power & LoRa (battery, signal strength)
 * - Page 3: Transport status (WiFi, BLE, MQTT, LoRa)
 * - Page 4: Relay, temperature, uptime, heap usage
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>

/**
 * @class OLEDManager
 * @brief Singleton OLED display manager with rotational UI
 */
class OLEDManager {
public:
    /**
     * @brief Singleton instance access
     */
    static OLEDManager& getInstance() {
        static OLEDManager instance;
        return instance;
    }

    /**
     * @brief Initialize OLED display and button interrupt
     * @return true if initialization successful, false otherwise
     */
    bool init();

    /**
     * @brief Refresh display (call periodically, e.g., every 500ms)
     */
    void update();

    /**
     * @brief Draw a boot progress bar on the splash screen
     */
    void drawBootProgress(const char* label, int percent);

    /**
     * @brief Show the boot splash screen
     */
    void showSplash(const char* ver, const char* role);

    /**
     * @brief Manually change to specific page (0-5)
     */
    void setPage(uint8_t pageNum);

    /**
     * @brief Get current page number
     */
    uint8_t getCurrentPage();

    /**
     * @brief Set brightness level
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current brightness level
     */
    uint8_t getBrightness();

    /**
     * @brief Force OLED on/off
     */
    void setDisplayOn(bool on);

    /**
     * @brief Check if OLED is currently on
     */
    bool isDisplayOn();

    /**
     * @brief Updates for various telemetry fields
     */
    void setIP(const char* ip);
    void setBatteryVoltage(float voltage, const char* mode);
    void setLoRaSignal(int8_t rssi, int8_t snr);
    void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora);
    void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora, bool espnow);
    void setRelayStatus(bool relayOn);
    void setTemperature(float tempC);
    void setPeerCount(uint8_t count);
    void setUptime(uint32_t uptimeMs);
    void setFreeHeap(uint32_t heapBytes);
    void setGPS(double lat, double lon, uint8_t sats, bool hasFix);
    void setDiagnostics(uint32_t bootCount, const char* reason);
    void setMAC(const char* mac);
    void setDeviceName(const char* name);
    void setVersion(const char* ver);
    void addLog(const char* msg);

    /**
     * @brief Print OLED manager status to Serial (diagnostics)
     */
    void printStatus();

private:
    // Private constructor (singleton pattern)
    OLEDManager();
};
