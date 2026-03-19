/**
 * @file display_manager.cpp
 * @brief OLED Display Manager Implementation
 *
 * 4-page rotating UI: STATUS / RADIO / MESH / POWER
 * Auto-rotates every 5 seconds. Supports on/off for CRITICAL power mode.
 */

#include "display_manager.h"
#include "../HAL/board_config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>

// ============================================================================
// Static Member Definitions
// ============================================================================

Adafruit_SSD1306 DisplayManager::_display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

DisplayPage  DisplayManager::_currentPage    = DisplayPage::STATUS;
uint32_t     DisplayManager::_lastPageChange = 0;
bool         DisplayManager::_initialized    = false;
bool         DisplayManager::_displayOn      = true;

String       DisplayManager::_nodeId         = "---";
String       DisplayManager::_ipAddress      = "0.0.0.0";
String       DisplayManager::_version        = FIRMWARE_VERSION;
int16_t      DisplayManager::_rssi           = -120;
float        DisplayManager::_snr            = 0.0f;
uint32_t     DisplayManager::_txCount        = 0;
uint32_t     DisplayManager::_rxCount        = 0;
uint8_t      DisplayManager::_neighborCount  = 0;
float        DisplayManager::_battVoltage    = 0.0f;
uint8_t      DisplayManager::_battPercent    = 0;
PowerMode    DisplayManager::_powerMode      = PowerMode::NORMAL;
bool         DisplayManager::_relay1         = false;
bool         DisplayManager::_relay2         = false;

// ============================================================================
// Public API
// ============================================================================

bool DisplayManager::begin() {
    // Initialize I2C bus with board-config pins
    Wire.begin(I2C_SDA, I2C_SCL);

    // Attempt to initialize the SSD1306 display
    if (!_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[DisplayMgr] SSD1306 init failed");
        _initialized = false;
        return false;
    }

    _initialized = true;
    _displayOn   = true;

    // Splash screen
    _display.clearDisplay();
    _display.setTextColor(SSD1306_WHITE);
    _display.setTextSize(2);
    _display.setCursor(4, 8);
    _display.println("LoRaLink v2");
    _display.setTextSize(1);
    _display.setCursor(20, 40);
    _display.println("Initializing...");
    _display.display();

    delay(2000);

    // Show first page
    _lastPageChange = millis();
    forceRefresh();

    Serial.println("[DisplayMgr] SSD1306 initialized");
    return true;
}

void DisplayManager::update() {
    if (!_initialized) {
        return;
    }

    uint32_t now = millis();

    // Auto-rotate page every PAGE_INTERVAL_MS
    if (now - _lastPageChange >= PAGE_INTERVAL_MS) {
        uint8_t next = (static_cast<uint8_t>(_currentPage) + 1) % DISPLAY_PAGES;
        _currentPage    = static_cast<DisplayPage>(next);
        _lastPageChange = now;
        forceRefresh();
    }
}

void DisplayManager::forceRefresh() {
    if (!_initialized) {
        return;
    }

    if (!_displayOn) {
        _display.clearDisplay();
        _display.display();
        return;
    }

    switch (_currentPage) {
        case DisplayPage::STATUS: _drawStatusPage(); break;
        case DisplayPage::RADIO:  _drawRadioPage();  break;
        case DisplayPage::MESH:   _drawMeshPage();   break;
        case DisplayPage::POWER:  _drawPowerPage();  break;
        default:
            _currentPage = DisplayPage::STATUS;
            _drawStatusPage();
            break;
    }
}

void DisplayManager::nextPage() {
    if (!_initialized) {
        return;
    }
    uint8_t next = (static_cast<uint8_t>(_currentPage) + 1) % DISPLAY_PAGES;
    _currentPage    = static_cast<DisplayPage>(next);
    _lastPageChange = millis();
    forceRefresh();
}

void DisplayManager::setPage(DisplayPage page) {
    if (!_initialized) {
        return;
    }
    _currentPage    = page;
    _lastPageChange = millis();
    forceRefresh();
}

DisplayPage DisplayManager::getCurrentPage() {
    return _currentPage;
}

// ============================================================================
// Data Setters
// ============================================================================

void DisplayManager::setNodeId(const String& id)         { _nodeId        = id; }
void DisplayManager::setIPAddress(const String& ip)      { _ipAddress     = ip; }
void DisplayManager::setVersion(const String& ver)       { _version       = ver; }
void DisplayManager::setRSSI(int16_t rssi)               { _rssi          = rssi; }
void DisplayManager::setSNR(float snr)                   { _snr           = snr; }
void DisplayManager::setTxCount(uint32_t count)          { _txCount       = count; }
void DisplayManager::setRxCount(uint32_t count)          { _rxCount       = count; }
void DisplayManager::setNeighborCount(uint8_t count)     { _neighborCount = count; }
void DisplayManager::setBatteryVoltage(float voltage)    { _battVoltage   = voltage; }
void DisplayManager::setBatteryPercent(uint8_t percent)  { _battPercent   = percent; }
void DisplayManager::setPowerModeDisplay(PowerMode mode) { _powerMode     = mode; }
void DisplayManager::setRelayStates(bool relay1, bool relay2) {
    _relay1 = relay1;
    _relay2 = relay2;
}

// ============================================================================
// Display On/Off
// ============================================================================

void DisplayManager::on() {
    if (!_initialized) {
        return;
    }
    _displayOn = true;
    _display.ssd1306_command(0xAF);  // Display ON command
    forceRefresh();
    Serial.println("[DisplayMgr] Display ON");
}

void DisplayManager::off() {
    if (!_initialized) {
        return;
    }
    _displayOn = false;
    _display.ssd1306_command(0xAE);  // Display OFF command
    Serial.println("[DisplayMgr] Display OFF");
}

bool DisplayManager::isOn() {
    return _displayOn;
}

// ============================================================================
// Private Helpers
// ============================================================================

/**
 * @brief Draw a page header: title text on row 0 then a horizontal rule.
 * Uses text size 1 (6x8 px per char). The rule sits at y=9.
 */
void DisplayManager::_drawHeader(const char* title) {
    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);
    _display.setCursor(0, 0);

    // Centre the title across 128 px (6 px/char)
    int16_t titleLen  = static_cast<int16_t>(strlen(title));
    int16_t titlePx   = titleLen * 6;
    int16_t startX    = (OLED_WIDTH - titlePx) / 2;
    if (startX < 0) startX = 0;

    _display.setCursor(startX, 0);
    _display.print(title);

    // Horizontal rule below header
    _display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);
}

// ============================================================================
// Page Renderers
// ============================================================================

/**
 * STATUS page layout (text size 1, rows start at y=12):
 *   [    STATUS    ]
 *   ─────────────────
 *   ID: Peer1_AB
 *   IP: 172.16.0.27
 *   VER: v0.3.0
 *   UP: 1h23m
 *   R1:ON  R2:OFF
 */
void DisplayManager::_drawStatusPage() {
    _display.clearDisplay();
    _drawHeader("[STATUS]");

    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);

    // Row 1: Node ID
    _display.setCursor(0, 12);
    _display.print("ID: ");
    _display.println(_nodeId);

    // Row 2: IP Address
    _display.print("IP: ");
    _display.println(_ipAddress);

    // Row 3: Firmware version
    _display.print("VER: ");
    _display.println(_version);

    // Row 4: Uptime derived from millis()
    uint32_t totalSecs = millis() / 1000UL;
    uint32_t hours     = totalSecs / 3600UL;
    uint32_t mins      = (totalSecs % 3600UL) / 60UL;
    char uptimeBuf[16];
    snprintf(uptimeBuf, sizeof(uptimeBuf), "%uh%um", (unsigned)hours, (unsigned)mins);
    _display.print("UP: ");
    _display.println(uptimeBuf);

    // Row 5: Relay states
    _display.print("R1:");
    _display.print(_relay1 ? "ON" : "OFF");
    _display.print("  R2:");
    _display.print(_relay2 ? "ON" : "OFF");

    _display.display();
}

/**
 * RADIO page layout:
 *   [    RADIO    ]
 *   ─────────────────
 *   RSSI: -87 dBm
 *   SNR:  8.5 dB
 *   TX: 142  RX: 1893
 *   CH: 0
 */
void DisplayManager::_drawRadioPage() {
    _display.clearDisplay();
    _drawHeader("[RADIO]");

    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);

    // Row 1: RSSI
    _display.setCursor(0, 12);
    _display.print("RSSI: ");
    _display.print(_rssi);
    _display.println(" dBm");

    // Row 2: SNR
    _display.print("SNR:  ");
    _display.print(_snr, 1);
    _display.println(" dB");

    // Row 3: TX / RX counts
    char countBuf[22];
    snprintf(countBuf, sizeof(countBuf), "TX:%-5lu RX:%-5lu",
             (unsigned long)_txCount, (unsigned long)_rxCount);
    _display.println(countBuf);

    // Row 4: Channel (placeholder — not tracked separately yet)
    _display.println("CH: 0");

    _display.display();
}

/**
 * MESH page layout:
 *   [    MESH    ]
 *   ─────────────────
 *   Neighbors: 3
 *   Hops: 2
 *   Last RX: 12s ago
 */
void DisplayManager::_drawMeshPage() {
    _display.clearDisplay();
    _drawHeader("[MESH]");

    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);

    // Row 1: Neighbor count
    _display.setCursor(0, 12);
    _display.print("Neighbors: ");
    _display.println(_neighborCount);

    // Row 2: Hop count (placeholder — sourced from mesh coordinator)
    _display.println("Hops: 1");

    // Row 3: Last RX elapsed (derived from millis vs last RX — placeholder)
    _display.println("Last RX: --");

    _display.display();
}

/**
 * POWER page layout:
 *   [    POWER    ]
 *   ─────────────────
 *   Bat: 3.78V (89%)
 *   Mode: NORMAL
 *   VEXT: ON
 */
void DisplayManager::_drawPowerPage() {
    _display.clearDisplay();
    _drawHeader("[POWER]");

    _display.setTextSize(1);
    _display.setTextColor(SSD1306_WHITE);

    // Row 1: Battery voltage + percent
    _display.setCursor(0, 12);
    char batBuf[22];
    snprintf(batBuf, sizeof(batBuf), "Bat: %.2fV (%u%%)",
             _battVoltage, (unsigned)_battPercent);
    _display.println(batBuf);

    // Row 2: Power mode name
    _display.print("Mode: ");
    switch (_powerMode) {
        case PowerMode::NORMAL:   _display.println("NORMAL");   break;
        case PowerMode::CONSERVE: _display.println("CONSERVE"); break;
        case PowerMode::CRITICAL: _display.println("CRITICAL"); break;
        default:                  _display.println("UNKNOWN");  break;
    }

    // Row 3: VEXT state — ON in NORMAL/CONSERVE, OFF in CRITICAL
    _display.print("VEXT: ");
    _display.println((_powerMode == PowerMode::CRITICAL) ? "OFF" : "ON");

    _display.display();
}
