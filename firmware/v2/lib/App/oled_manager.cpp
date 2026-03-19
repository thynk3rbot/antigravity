/**
 * @file oled_manager.cpp
 * @brief OLED Display Manager Implementation
 *
 * Implements 4-page rotating display with button control and auto-rotation.
 * Pages update every 500ms; button debouncing at 50ms.
 * Auto-rotate timer resets on manual page changes.
 */

#include "oled_manager.h"
#include "../HAL/board_config.h"
#include "power_manager.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <esp_heap_caps.h>

// ============================================================================
// Static Instance & State
// ============================================================================

static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

// Cached display values (updated via setter functions)
struct CachedValues {
    char ip[32];              // Network IP
    float batVoltage;         // Battery voltage (V)
    char powerMode[16];       // Power mode string
    int8_t loraRSSI;          // LoRa RSSI (dBm)
    int8_t loraSNR;           // LoRa SNR (dB)
    bool wifiActive;          // WiFi status
    bool bleActive;           // BLE status
    bool mqttActive;          // MQTT status
    bool loraActive;          // LoRa status
    bool relayOn;             // Relay status
    float tempC;              // Temperature
    uint8_t peerCount;        // Peer/node count
    uint32_t uptimeMs;        // System uptime
    uint32_t freeHeapBytes;   // Free heap
};

static CachedValues g_cached = {};  // Zero-initialized, then set defaults

void initCachedValues() {
    strncpy(g_cached.ip, "N/A", sizeof(g_cached.ip) - 1);
    g_cached.batVoltage = 0.0f;
    strncpy(g_cached.powerMode, "NORMAL", sizeof(g_cached.powerMode) - 1);
    g_cached.loraRSSI = -120;
    g_cached.loraSNR = 0;
    g_cached.wifiActive = false;
    g_cached.bleActive = false;
    g_cached.mqttActive = false;
    g_cached.loraActive = false;
    g_cached.relayOn = false;
    g_cached.tempC = 0.0f;
    g_cached.peerCount = 0;
    g_cached.uptimeMs = 0;
    g_cached.freeHeapBytes = 0;
}

// Display state
static uint8_t g_currentPage = 0;
static uint8_t g_brightness = 127;  // Default brightness (0-255)
static bool g_displayOn = true;
static uint32_t g_lastUpdateTime = 0;
static uint32_t g_lastAutoRotateTime = 0;
static const uint32_t UPDATE_INTERVAL_MS = 500;

// Button state for debouncing and long-press detection
static uint32_t g_buttonPressTime = 0;
static bool g_buttonPressed = false;

// ============================================================================
// Forward Declarations
// ============================================================================

static void displayPage1();  // Network Status
static void displayPage2();  // Power & LoRa
static void displayPage3();  // Transport Status
static void displayPage4();  // Relay & Temperature

// ============================================================================
// Button Interrupt Handler
// ============================================================================

static void IRAM_ATTR buttonISRHandler() {
    // Called when GPIO_0 changes state
    // This is a simplified ISR; debouncing is handled in main update loop
    uint32_t now = millis();

    if (digitalRead(BUTTON_PIN) == LOW) {
        // Button pressed
        if (!g_buttonPressed) {
            g_buttonPressTime = now;
            g_buttonPressed = true;
        }
    } else {
        // Button released
        if (g_buttonPressed) {
            uint32_t pressDuration = now - g_buttonPressTime;

            // Debounce threshold
            if (pressDuration >= BUTTON_DEBOUNCE_MS) {
                if (pressDuration >= BUTTON_LONG_PRESS_MS) {
                    // Long press: toggle brightness
                    if (g_brightness >= 200) {
                        g_brightness = 50;
                    } else if (g_brightness >= 127) {
                        g_brightness = 200;
                    } else {
                        g_brightness = 127;
                    }
                    Serial.printf("[OLED] Brightness toggled: %u\n", g_brightness);
                } else {
                    // Short press: rotate to next page
                    g_currentPage = (g_currentPage + 1) % 4;
                    g_lastAutoRotateTime = millis();  // Reset auto-rotate timer
                    Serial.printf("[OLED] Page changed to: %u\n", g_currentPage);
                }
            }
            g_buttonPressed = false;
        }
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Draw horizontal line at Y position
 */
static void drawHLine(int16_t y) {
    display.drawLine(0, y, OLED_WIDTH - 1, y, SSD1306_WHITE);
}

// ============================================================================
// Page Display Functions
// ============================================================================

/**
 * @brief Page 1: Network Status
 */
static void displayPage1() {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // Header
    display.setCursor(0, 0);
    display.println("LoRaLink");

    drawHLine(16);

    // Network info
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("IP: ");
    display.println(g_cached.ip);

    display.print("Peers: ");
    display.println(g_cached.peerCount);

    display.print("WiFi: ");
    display.println(g_cached.wifiActive ? "ON" : "OFF");

    display.print("Page: 1/4");

    display.display();
}

/**
 * @brief Page 2: Power & LoRa Signal
 */
static void displayPage2() {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // Header
    display.setCursor(0, 0);
    display.println("Power & LoRa");

    drawHLine(16);

    // Power info
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("Batt: ");
    display.print(g_cached.batVoltage, 2);
    display.println("V");

    display.print("Mode: ");
    display.println(g_cached.powerMode);

    display.print("RSSI: ");
    display.print(g_cached.loraRSSI);
    display.println(" dBm");

    display.print("SNR: ");
    display.print(g_cached.loraSNR);
    display.println(" dB");

    display.print("Page: 2/4");

    display.display();
}

/**
 * @brief Page 3: Transport Status
 */
static void displayPage3() {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // Header
    display.setCursor(0, 0);
    display.println("Transports");

    drawHLine(16);

    // Transport status
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("WiFi: ");
    display.println(g_cached.wifiActive ? "ON" : "OFF");

    display.print("BLE:  ");
    display.println(g_cached.bleActive ? "ON" : "OFF");

    display.print("MQTT: ");
    display.println(g_cached.mqttActive ? "ON" : "OFF");

    display.print("LoRa: ");
    display.println(g_cached.loraActive ? "ON" : "OFF");

    display.print("Page: 3/4");

    display.display();
}

/**
 * @brief Page 4: Relay, Temperature, Uptime, Heap
 */
static void displayPage4() {
    display.clearDisplay();
    display.setFont();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // Header
    display.setCursor(0, 0);
    display.println("System Status");

    drawHLine(16);

    // System info
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("Relay: ");
    display.println(g_cached.relayOn ? "ON" : "OFF");

    display.print("Temp: ");
    display.print(g_cached.tempC, 1);
    display.println("C");

    // Uptime in hours:minutes
    uint32_t totalSecs = g_cached.uptimeMs / 1000;
    uint32_t hours = totalSecs / 3600;
    uint32_t mins = (totalSecs % 3600) / 60;
    display.printf("Uptime: %uh%um\n", (unsigned int)hours, (unsigned int)mins);

    display.print("Heap: ");
    display.print(g_cached.freeHeapBytes / 1024);
    display.println(" KB");

    display.print("Page: 4/4");

    display.display();
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool OLEDManager::init() {
    // Initialize cached values
    initCachedValues();

    // Initialize I2C with board config pins
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    delay(100);

    // Initialize display
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[OLED] SSD1306 allocation failed");
        return false;
    }

    // Initialize display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initializing OLED...");
    display.display();

    Serial.println("[OLED] SSD1306 initialized successfully");

    // Initialize button with interrupt
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISRHandler, CHANGE);

    Serial.println("[OLED] Button interrupt attached to GPIO_0");

    // Initialize timers
    g_lastUpdateTime = millis();
    g_lastAutoRotateTime = millis();

    // Display initial page
    g_currentPage = 0;
    displayPage1();

    return true;
}

void OLEDManager::update() {
    uint32_t now = millis();

    // Update display periodically
    if (now - g_lastUpdateTime >= UPDATE_INTERVAL_MS) {
        g_lastUpdateTime = now;

        // Apply brightness (via dim if needed)
        // Note: Adafruit_SSD1306 has limited brightness control
        // Full control via ssd1306_command() if needed
        if (g_brightness < 127) {
            display.dim(true);
        } else {
            display.dim(false);
        }

        // Auto-rotate if no user interaction
        if (now - g_lastAutoRotateTime >= OLED_AUTO_ROTATE_MS) {
            g_currentPage = (g_currentPage + 1) % 4;
            g_lastAutoRotateTime = now;
        }

        // Render current page
        if (g_displayOn) {
            switch (g_currentPage) {
                case 0:
                    displayPage1();
                    break;
                case 1:
                    displayPage2();
                    break;
                case 2:
                    displayPage3();
                    break;
                case 3:
                    displayPage4();
                    break;
                default:
                    g_currentPage = 0;
                    displayPage1();
                    break;
            }
        } else {
            display.clearDisplay();
            display.display();
        }
    }
}

void OLEDManager::setPage(uint8_t pageNum) {
    if (pageNum >= 4) {
        return;  // Invalid page
    }
    g_currentPage = pageNum;
    g_lastAutoRotateTime = millis();  // Reset auto-rotate timer
    Serial.printf("[OLED] Page set to: %u\n", pageNum);
}

uint8_t OLEDManager::getCurrentPage() {
    return g_currentPage;
}

void OLEDManager::setBrightness(uint8_t brightness) {
    g_brightness = brightness;
    Serial.printf("[OLED] Brightness set to: %u\n", g_brightness);
}

uint8_t OLEDManager::getBrightness() {
    return g_brightness;
}

void OLEDManager::setDisplayOn(bool on) {
    g_displayOn = on;
    if (on) {
        display.ssd1306_command(0xAF);  // Display ON
        Serial.println("[OLED] Display ON");
    } else {
        display.ssd1306_command(0xAE);  // Display OFF
        Serial.println("[OLED] Display OFF");
    }
}

bool OLEDManager::isDisplayOn() {
    return g_displayOn;
}

void OLEDManager::setIP(const char* ip) {
    if (ip != nullptr) {
        strncpy(g_cached.ip, ip, sizeof(g_cached.ip) - 1);
        g_cached.ip[sizeof(g_cached.ip) - 1] = '\0';
    }
}

void OLEDManager::setBatteryVoltage(float voltage, const char* mode) {
    g_cached.batVoltage = voltage;
    if (mode != nullptr) {
        strncpy(g_cached.powerMode, mode, sizeof(g_cached.powerMode) - 1);
        g_cached.powerMode[sizeof(g_cached.powerMode) - 1] = '\0';
    }
}

void OLEDManager::setLoRaSignal(int8_t rssi, int8_t snr) {
    g_cached.loraRSSI = rssi;
    g_cached.loraSNR = snr;
}

void OLEDManager::setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora) {
    g_cached.wifiActive = wifi;
    g_cached.bleActive = ble;
    g_cached.mqttActive = mqtt;
    g_cached.loraActive = lora;
}

void OLEDManager::setRelayStatus(bool relayOn) {
    g_cached.relayOn = relayOn;
}

void OLEDManager::setTemperature(float tempC) {
    g_cached.tempC = tempC;
}

void OLEDManager::setPeerCount(uint8_t count) {
    g_cached.peerCount = count;
}

void OLEDManager::setUptime(uint32_t uptimeMs) {
    g_cached.uptimeMs = uptimeMs;
}

void OLEDManager::setFreeHeap(uint32_t heapBytes) {
    g_cached.freeHeapBytes = heapBytes;
}

void OLEDManager::printStatus() {
    Serial.println("\n[OLED Status]");
    Serial.printf("  Current Page: %u\n", g_currentPage);
    Serial.printf("  Brightness: %u\n", g_brightness);
    Serial.printf("  Display On: %s\n", g_displayOn ? "Yes" : "No");
    Serial.printf("  IP: %s\n", g_cached.ip);
    Serial.printf("  Battery: %.2f V (%s)\n", g_cached.batVoltage, g_cached.powerMode);
    Serial.printf("  LoRa RSSI: %d dBm, SNR: %d dB\n", g_cached.loraRSSI, g_cached.loraSNR);
    Serial.printf("  WiFi: %s, BLE: %s, MQTT: %s, LoRa: %s\n",
                  g_cached.wifiActive ? "ON" : "OFF",
                  g_cached.bleActive ? "ON" : "OFF",
                  g_cached.mqttActive ? "ON" : "OFF",
                  g_cached.loraActive ? "ON" : "OFF");
    Serial.printf("  Relay: %s, Temp: %.1f°C\n", g_cached.relayOn ? "ON" : "OFF", g_cached.tempC);
    Serial.printf("  Peers: %u, Uptime: %u ms, Free Heap: %u bytes\n",
                  g_cached.peerCount, (unsigned int)g_cached.uptimeMs, (unsigned int)g_cached.freeHeapBytes);
    Serial.println();
}
