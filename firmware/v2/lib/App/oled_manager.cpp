#include "oled_manager.h"
#include "board_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../HAL/i2c_mutex.h"
#include "power_manager.h"
#include <cstring>
#include <cstdint>

// Display configuration
#define OLED_WIDTH    128
#define OLED_HEIGHT   64
#define OLED_ADDRESS  0x3C

// Native display object
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

// Cached data for display
struct DisplayCache {
    char deviceName[20];
    char macSuffix[10];
    char version[12];
    char ip[16];
    uint8_t peerCount;
    bool wifiActive;
    bool bleActive;
    bool mqttActive;
    bool loraActive;
    bool espnowActive;
    float batVoltage;
    char powerMode[12];
    float tempC;
    uint32_t uptimeMs;
    uint32_t freeHeapBytes;
    uint32_t bootCount;
    char resetReason[20];
    uint8_t relayMask;   // 8-bit bitmask, bit N = relay channel N
#ifdef HAS_GPS
    double gpsLat;
    double gpsLon;
    uint8_t gpsSats;
    bool gpsFix;
#endif
    int8_t loraRSSI;
    float loraSNR;
} g_cached;

// Display state
static uint8_t g_currentPage = 0;
static uint8_t g_brightness = 255;
static bool g_displayOn = true;
static bool g_wakeRequested = false;
static uint32_t g_lastUpdateTime = 0;
static uint32_t g_lastAutoRotateTime = 0;
static uint32_t g_lastActivityTime = 0;
static volatile bool g_displayNeedsRefresh = false;  // Flag for deferred I2C send

static const uint32_t UPDATE_INTERVAL_MS = 1000; // Stabilize at 1Hz
static const uint32_t SLEEP_TIMEOUT_MS = 30000;
#define AUTO_ROTATE_MS 0   // DISABLED: Manual mode only

static void displayPage1();
static void displayPage2();
static void displayPage3();
static void displayPage4();
#ifdef HAS_GPS
static void displayPage5();
#endif
static void displayPage6();

// ============================================================================
// Internal Helpers
// ============================================================================

static void drawHeader(const char* title) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    
    // Show [USB] if plugged in
    if (PowerManager::isPowered()) {
        display.printf("%s [USB] : %s", g_cached.deviceName, title);
    } else {
        display.printf("%s : %s", g_cached.deviceName, title);
    }
    
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

static void drawFooter(uint8_t pageNum) {
    display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    display.setCursor(0, 56);
    display.printf("v%s | Page %u/%u", g_cached.version, pageNum, 
#ifdef HAS_GPS
        6
#else
        5
#endif
    );
}

static void displayPage1() {
    display.clearDisplay();
    drawHeader("Network");
    display.setCursor(0, 14);
    display.printf("%s\n", g_cached.ip);
    display.printf("%u connected\n", g_cached.peerCount);
    display.printf("%s\n", g_cached.wifiActive ? "ACTIVE" : "OFF");
    drawFooter(1);
    display.display();
}

static void displayPage2() {
    display.clearDisplay();
    drawHeader("Power & LoRa");
    display.setCursor(0, 14);
    display.printf("%.2f V\n", g_cached.batVoltage);
    display.printf("%d dBm\n", g_cached.loraRSSI);
    display.printf("%.1f dB SNR\n", g_cached.loraSNR);
    drawFooter(2);
    display.display();
}

static void displayPage3() {
    display.clearDisplay();
    drawHeader("Transports");
    display.setCursor(0, 14);
    display.printf("BLE   %s\n", g_cached.bleActive ? "ON" : "OFF");
    display.printf("MQTT  %s\n", g_cached.mqttActive ? "ON" : "OFF");
    display.printf("LoRa  %s\n", g_cached.loraActive ? "ON" : "OFF");
    display.printf("ESPNW %s\n", g_cached.espnowActive ? "ON" : "OFF");
    drawFooter(3);
    display.display();
}

static void displayPage4() {
    display.clearDisplay();
    drawHeader("Relays");
    display.setCursor(0, 14);

    // Relay bitmask — 8 channels as filled/empty squares, 14px wide each
    // ■ = ON (filled),  □ = OFF (outline)
    for (uint8_t i = 0; i < 8; i++) {
        int16_t x = i * 15;
        bool on = (g_cached.relayMask >> i) & 0x01;
        if (on) {
            display.fillRect(x, 14, 12, 12, SSD1306_WHITE);
        } else {
            display.drawRect(x, 14, 12, 12, SSD1306_WHITE);
        }
        // Channel label below box
        display.setCursor(x + 3, 27);
        display.printf("%u", i + 1);
    }

    // Active count + temp on line below
    display.setCursor(0, 38);
    display.printf("%u active  %.1fC  %uKB",
        g_cached.relayMask ? __builtin_popcount(g_cached.relayMask) : 0,
        g_cached.tempC,
        g_cached.freeHeapBytes / 1024);

    drawFooter(4);
    display.display();
}

#ifdef HAS_GPS
static void displayPage5() {
    display.clearDisplay();
    
    // Custom Meshtastic-Style GPS UI Header
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    
    if (PowerManager::isPowered()) {
        display.printf("%s [USB] | GPS", g_cached.deviceName);
    } else {
        display.printf("%s | GPS", g_cached.deviceName);
    }
    
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    if (!g_cached.gpsFix && g_cached.gpsSats == 0) {
        // No Sats View
        display.setCursor(20, 30);
        display.print("No Satellites");
        display.drawCircle(64, 34, 15, SSD1306_WHITE);
        display.drawLine(54, 34, 74, 34, SSD1306_WHITE); // Horizontal
        display.drawLine(64, 24, 64, 44, SSD1306_WHITE); // Vertical
    } else {
        // Detailed GPS View
        display.setCursor(0, 14);
        display.printf("Lat: %.5f\n", g_cached.gpsLat);
        display.setCursor(0, 24);
        display.printf("Lon: %.5f\n", g_cached.gpsLon);
        
        // Status Right Side
        display.setCursor(85, 14);
        display.printf("Sats:%u\n", g_cached.gpsSats);
        display.setCursor(85, 24);
        if (g_cached.gpsFix) {
            display.print("Fix:3D");
            // Draw a tiny compass/radar graphic for 3D Fix
            display.drawCircle(110, 42, 10, SSD1306_WHITE);
            display.fillCircle(110, 42, 2, SSD1306_WHITE);
            // Simulated Heading Needle
            display.drawLine(110, 42, 110, 32, SSD1306_WHITE); 
        } else {
            display.print("Fix:NO");
            display.drawCircle(110, 42, 10, SSD1306_WHITE);
        }
        
        display.setCursor(0, 36);
        display.printf("Alt: --- m\n"); // Placeholder for altitude
    }
    
    drawFooter(5);
    display.display();
}
#endif

static void displayPage6() {
    display.clearDisplay();
    drawHeader("Diag");
    display.setCursor(0, 14);
    display.printf("Up: %u min\n", g_cached.uptimeMs / 60000);
    display.printf("Boot: %u\n", g_cached.bootCount);
    display.printf("Heap: %u KB\n", g_cached.freeHeapBytes / 1024);
    
#ifdef HAS_GPS
    drawFooter(6);
#else
    drawFooter(5);
#endif
    display.display();
}

// Interrupt handler for button (Debounced via update loop)
static volatile bool g_buttonHandled = false;
static volatile uint32_t g_lastInterruptTime = 0;

static void IRAM_ATTR buttonISR() {
    uint32_t now = millis();
    if (now - g_lastInterruptTime > 200) { // 200ms debounce
        g_wakeRequested = true;
        g_buttonHandled = false;
        g_lastInterruptTime = now;
    }
}

// ============================================================================
// Public API
// ============================================================================

OLEDManager::InitState OLEDManager::_initState = OLEDManager::InitState::IDLE;
uint32_t OLEDManager::_stateStartTime = 0;

OLEDManager::OLEDManager() {
    memset(&g_cached, 0, sizeof(g_cached));
}

bool OLEDManager::init() {
    _initState = InitState::RESET_LOW;
    _stateStartTime = millis();
    
    Serial.println("[UI] Performing Hardened OLED Reset...");
    if (OLED_RESET_PIN != -1) {
        pinMode(OLED_RESET_PIN, OUTPUT);
        digitalWrite(OLED_RESET_PIN, LOW);
        delay(200);   // v0.4.0: Increased reset pulse duration for S3-V4 hardware
        digitalWrite(OLED_RESET_PIN, HIGH);
        delay(200);   // Stability delay after reset
    }
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    pinMode(BUTTON_PIN, INPUT);
#endif
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
    
    // Wire.begin should have happened in BootSequence, we MUST NOT call it again (prevents OLED glitch)
    I2C_LOCK();
    
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(0xFF); // Universal max contrast for debug
        
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0,0);
        display.println("Magic v2");
        display.println("Starting Mesh...");
        display.display();
        _initState = InitState::RUNNING;
        Serial.println("  ✓ OLED Hardware initialized and cleared");
    } else {
        Serial.println("  ! OLED Hardware begin failed - check VEXT/I2C");
        _initState = InitState::START_I2C; 
    }
    I2C_UNLOCK();
    
    g_lastUpdateTime = millis();
    g_lastActivityTime = millis();
    g_lastAutoRotateTime = millis();
    g_displayOn = true;
    return true;
}

void OLEDManager::_processInit() {
    uint32_t now = millis();
    
    switch (_initState) {
        case InitState::RESET_LOW:
            if (now - _stateStartTime >= 50) { // Increased to 50ms for V2 stability
                if (OLED_RESET_PIN != -1) digitalWrite(OLED_RESET_PIN, HIGH);
                _stateStartTime = now;
                _initState = InitState::RESET_HIGH;
            }
            break;
            
        case InitState::RESET_HIGH:
            if (now - _stateStartTime >= 20) {
                _initState = InitState::WAIT_POWER;
            }
            break;
            
        case InitState::WAIT_POWER:
            if (PowerManager::isVEXTStable()) {
                _initState = InitState::START_I2C;
            }
            break;
            
        case InitState::START_I2C:
            // Only retry display.begin, do NOT call Wire.begin again
            if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
                display.ssd1306_command(SSD1306_SETCONTRAST);
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
                display.ssd1306_command(0xFF);
                display.ssd1306_command(SSD1306_SETPRECHARGE);
                display.ssd1306_command(0xF1); // Improved precharge for V2 longevity
                display.ssd1306_command(SSD1306_SETVCOMDETECT);
                display.ssd1306_command(0x40); // Higher VCOM for better contrast
#else
                display.ssd1306_command(0xCF);
#endif
                display.clearDisplay();
                display.setTextColor(SSD1306_WHITE);
                _initState = InitState::RUNNING;
            } else {
                _stateStartTime = now;
                _initState = InitState::WAIT_POWER; 
            }
            break;
            
        default:
            break;
    }
}

void OLEDManager::showSplash(const char* ver, const char* role) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.drawFastHLine(0, 15, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 48, 128, SSD1306_WHITE);
    
    display.setTextSize(1);
    display.setCursor(5, 4);
    display.print("SYSTEM BOOT");
    
    display.setTextSize(2);
    display.setCursor(15, 22);
    display.print("Magic");
    
    display.setTextSize(1);
    display.setCursor(5, 52);
    display.printf("v%s | %s", ver, role);
    
    display.display();
}

void OLEDManager::drawBootProgress(const char* label, int percent) {
    if (!g_displayOn) return;
    display.setTextColor(SSD1306_WHITE);
    display.fillRect(2, 49, 124, 14, SSD1306_BLACK); 
    display.setTextSize(1);
    display.setCursor(5, 52);
    display.printf("%s.. %d%%", label, percent);
    int barWidth = (percent * 118) / 100;
    display.drawRect(5, 60, 118, 3, SSD1306_WHITE);
    display.fillRect(5, 60, barWidth, 3, SSD1306_WHITE);
    display.display();
}

void OLEDManager::update() {
    if (_initState != InitState::RUNNING) {
        _processInit();
        if (_initState != InitState::RUNNING) return; // Wait for setup to finish
    }

    uint32_t now = millis();
    if (g_wakeRequested) { setDisplayOn(true); g_wakeRequested = false; }
    // Only sleep if not USB powered
    if (g_displayOn && !PowerManager::isPowered() && (now - g_lastActivityTime >= SLEEP_TIMEOUT_MS)) setDisplayOn(false);
    // Keep display alive when USB connected
    if (PowerManager::isPowered() && !g_displayOn) { setDisplayOn(true); g_lastActivityTime = now; }

    // Handle button press detected by ISR
    if (!g_buttonHandled) {
        g_buttonHandled = true;
        g_lastActivityTime = now;
        if (!g_displayOn) {
            setDisplayOn(true);
        } else {
            // Manual Rotation
#ifdef HAS_GPS
            g_currentPage = (g_currentPage + 1) % 6;
#else
            g_currentPage = (g_currentPage + 1) % 5;
#endif
            g_lastAutoRotateTime = now;
        }
    }

#if defined(AUTO_ROTATE_MS) && (AUTO_ROTATE_MS > 0)
    if (g_displayOn && (now - g_lastAutoRotateTime >= AUTO_ROTATE_MS)) {
        // Auto Rotation
#ifdef HAS_GPS
        g_currentPage = (g_currentPage + 1) % 6;
#else
        g_currentPage = (g_currentPage + 1) % 5;
#endif
        g_lastAutoRotateTime = now;
    }
#endif

    if (now - g_lastUpdateTime >= UPDATE_INTERVAL_MS || g_wakeRequested) {
        g_lastUpdateTime = now;
        if (g_displayOn) {
            // Buffer-only writes (no I2C). Mutex NOT required here as data race 
            // with displayTask is transient and non-blocking.
            switch (g_currentPage) {
                case 0: displayPage1(); break;
                case 1: displayPage2(); break;
                case 2: displayPage3(); break;
                case 3: displayPage4(); break;
                case 4:
#ifdef HAS_GPS
                    displayPage5(); break;
#else
                    displayPage6(); break;
#endif
                case 5: displayPage6(); break;
                default: g_currentPage = 0; break;
            }
            // Set flag for deferred I2C send (happens in low-priority displayTask)
            g_displayNeedsRefresh = true;
        }
    }
}

void OLEDManager::deferredRefresh() {
    // Low-priority task safe to block here
    if (g_displayOn && g_displayNeedsRefresh) {
        I2C_LOCK();
        display.display();  // 50-100ms blocking I2C operation
        I2C_UNLOCK();
        g_displayNeedsRefresh = false;
    }
}

void OLEDManager::setPage(uint8_t pageNum) { if (pageNum < 6) g_currentPage = pageNum; }
uint8_t OLEDManager::getCurrentPage() { return g_currentPage; }
void OLEDManager::setBrightness(uint8_t brightness) { g_brightness = brightness; }
uint8_t OLEDManager::getBrightness() { return g_brightness; }
void OLEDManager::setDisplayOn(bool on) { g_displayOn = on; display.ssd1306_command(on ? 0xAF : 0xAE); }
bool OLEDManager::isDisplayOn() { return g_displayOn; }
void OLEDManager::setIP(const char* ip) { if (ip) strncpy(g_cached.ip, ip, 15); }
void OLEDManager::setBatteryVoltage(float voltage, const char* mode) { g_cached.batVoltage = voltage; if (mode) strncpy(g_cached.powerMode, mode, 11); }
void OLEDManager::setLoRaSignal(int8_t rssi, int8_t snr) { g_cached.loraRSSI = rssi; g_cached.loraSNR = (float)snr; }

void OLEDManager::setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora) {
    g_cached.wifiActive = wifi;
    g_cached.bleActive = ble;
    g_cached.mqttActive = mqtt;
    g_cached.loraActive = lora;
}

void OLEDManager::setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora, bool espnow) {
    g_cached.wifiActive = wifi; g_cached.bleActive = ble; g_cached.mqttActive = mqtt; g_cached.loraActive = lora; g_cached.espnowActive = espnow;
}

void OLEDManager::setRelayStatus(uint8_t relayMask) { g_cached.relayMask = relayMask; }
void OLEDManager::setTemperature(float tempC) { g_cached.tempC = tempC; }
void OLEDManager::setPeerCount(uint8_t count) { g_cached.peerCount = count; }
void OLEDManager::setUptime(uint32_t uptimeMs) { g_cached.uptimeMs = uptimeMs; }
void OLEDManager::setFreeHeap(uint32_t heapBytes) { g_cached.freeHeapBytes = heapBytes; }
#ifdef HAS_GPS
void OLEDManager::setGPS(double lat, double lon, uint8_t sats, bool hasFix) { g_cached.gpsLat = lat; g_cached.gpsLon = lon; g_cached.gpsSats = sats; g_cached.gpsFix = hasFix; }
#endif
void OLEDManager::setDiagnostics(uint32_t bootCount, const char* reason) { g_cached.bootCount = bootCount; if (reason) strncpy(g_cached.resetReason, reason, 19); }
void OLEDManager::setMAC(const char* mac) { if (mac) strncpy(g_cached.macSuffix, mac, 9); }
void OLEDManager::setDeviceName(const char* name) { if (name) strncpy(g_cached.deviceName, name, 19); }
void OLEDManager::setVersion(const char* ver) { if (ver) strncpy(g_cached.version, ver, 11); }
void OLEDManager::addLog(const char* msg) {}
void OLEDManager::printStatus() {}
