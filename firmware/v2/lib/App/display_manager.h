/**
 * @file display_manager.h
 * @brief OLED Display Manager with 4-Page Rotating UI
 *
 * Manages a 128x64 SSD1306 OLED display showing device status across 4 pages:
 * - STATUS: Node ID, IP, firmware version, uptime, relay states
 * - RADIO:  LoRa RSSI, SNR, TX/RX packet counts, channel
 * - MESH:   Neighbor count, hop count, last RX elapsed time
 * - POWER:  Battery voltage/percent, power mode, VEXT state
 *
 * Auto-rotates every 5 seconds. Display can be turned off in CRITICAL power mode.
 */

#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "power_manager.h"

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64
#define DISPLAY_PAGES  4

enum class DisplayPage : uint8_t {
    STATUS  = 0,  // Node ID, IP, version, uptime
    RADIO   = 1,  // LoRa RSSI, SNR, TX/RX counts, channel
    MESH    = 2,  // Neighbor count, hop count, last seen
    POWER   = 3,  // Battery voltage/%, power mode, VEXT state
};

class DisplayManager {
public:
    static bool begin();              // Init display, returns false if no display
    static void update();            // Call in main loop - handles page rotation
    static void forceRefresh();      // Redraw current page immediately
    static void nextPage();          // Advance to next page manually
    static void setPage(DisplayPage page);
    static DisplayPage getCurrentPage();

    // Data setters - call to update displayed values
    static void setNodeId(const String& id);
    static void setIPAddress(const String& ip);
    static void setVersion(const String& ver);
    static void setRSSI(int16_t rssi);
    static void setSNR(float snr);
    static void setTxCount(uint32_t count);
    static void setRxCount(uint32_t count);
    static void setNeighborCount(uint8_t count);
    static void setBatteryVoltage(float voltage);
    static void setBatteryPercent(uint8_t percent);
    static void setPowerModeDisplay(PowerMode mode);
    static void setRelayStates(bool relay1, bool relay2);

    // Display on/off (used by power manager in CRITICAL mode)
    static void on();
    static void off();
    static bool isOn();

private:
    static Adafruit_SSD1306 _display;
    static DisplayPage _currentPage;
    static uint32_t _lastPageChange;
    static bool _initialized;
    static bool _displayOn;

    // Page rotation interval
    static constexpr uint32_t PAGE_INTERVAL_MS = 5000;  // 5 seconds per page

    // Stored display data
    static String _nodeId;
    static String _ipAddress;
    static String _version;
    static int16_t _rssi;
    static float _snr;
    static uint32_t _txCount;
    static uint32_t _rxCount;
    static uint8_t _neighborCount;
    static float _battVoltage;
    static uint8_t _battPercent;
    static PowerMode _powerMode;
    static bool _relay1;
    static bool _relay2;

    // Page renderers
    static void _drawStatusPage();
    static void _drawRadioPage();
    static void _drawMeshPage();
    static void _drawPowerPage();
    static void _drawHeader(const char* title);
};
