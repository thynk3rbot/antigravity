#include "oled_manager.h"
#include <Arduino.h>
#include <cstdint>
#include "board_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../HAL/i2c_mutex.h"
#include "power_manager.h"

// Display configuration
#define OLED_WIDTH    128
#define OLED_HEIGHT   64
#define OLED_ADDRESS  0x3C

// Native display object
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

// Cached data for display
struct DisplayCache {
    char deviceName[20];
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
    uint8_t batPercent;
    uint32_t uptimeMs;
    uint32_t freeHeapBytes;
    char lastMsg[32];
    uint8_t lastMsgSrc;
    uint8_t lastMsgType;
#ifdef HAS_GPS
    double gpsLat;
    double gpsLon;
    double gpsAlt;
    uint8_t gpsSats;
    bool gpsFix;
    uint32_t gpsAge;
#endif
    int8_t loraRSSI;
    float loraSNR;
    int8_t rssiHistory[5];
    uint32_t bootCount;
    char resetReason[20];
    char macSuffix[10];
} g_cached;

// Display state
static uint8_t g_currentPage = 0;
static uint8_t g_brightness = 255;
static bool g_displayOn = true;
static bool g_wakeRequested = false;
static uint32_t g_lastUpdateTime = 0;
static uint32_t g_lastAutoRotateTime = 0;
static uint32_t g_lastActivityTime = 0;

static const uint32_t UPDATE_INTERVAL_MS = 1000;
static const uint32_t SLEEP_TIMEOUT_MS = 30000;
#define AUTO_ROTATE_MS 0

static void displayPage0();
static void displayPage1();
static void displayPage2();
static void displayPage3();
static void displayPage4();
#ifdef HAS_GPS
static void displayPage5();
#endif
static void displayPage7();

#ifdef HAS_GPS
#define MAX_PAGE 5
#else
#define MAX_PAGE 4
#endif

// ============================================================================
// Internal Helpers
// ============================================================================

static void drawHeader(const char* title) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("%s | %s", g_cached.deviceName, title);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

static void drawFooter(uint8_t pageNum) {
    display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    display.setCursor(0, 56);
    display.printf("v%s | P%u/%u", g_cached.version, pageNum + 1, 
#ifdef HAS_GPS
        6
#else
        5
#endif
    );
}

static void displayPage0() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.printf("%s\n", g_cached.deviceName);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.printf("ID:   %s\n", g_cached.macSuffix);
    display.printf("Bat:  %u%%\n", g_cached.batPercent);
    display.printf("Mode: %s\n", g_cached.powerMode);
    drawFooter(0);
}

static void displayPage1() {
    display.clearDisplay();
    drawHeader("Network");
    display.setCursor(0, 14);
    display.printf("IP:    %s\n", g_cached.ip);
    display.printf("Peers: %u connected\n", g_cached.peerCount);
    display.printf("RSSI:  %d dBm\n", g_cached.loraRSSI);
    drawFooter(1);
}

static void displayPage2() {
    display.clearDisplay();
    drawHeader("Radio");
    display.setCursor(0, 14);
    display.printf("RSSI: %d dBm\n", g_cached.loraRSSI);
    display.printf("SNR:  %.1f dB\n", g_cached.loraSNR);
    display.print("Hist: ");
    for(int i=0; i<5; i++) display.printf("%d ", g_cached.rssiHistory[i]);
    drawFooter(2);
}

static void displayPage3() {
    display.clearDisplay();
    drawHeader("Power");
    display.setCursor(0, 14);
    display.printf("Level: %u%%\n", g_cached.batPercent);
    display.printf("Mode:  %s\n", g_cached.powerMode);
    drawFooter(3);
}

static void displayPage4() {
    display.clearDisplay();
    drawHeader("Info");
    display.setCursor(0, 14);
    display.printf("Heap: %u KB\n", g_cached.freeHeapBytes / 1024);
    drawFooter(4);
}

#ifdef HAS_GPS
static void displayPage5() {
    display.clearDisplay();
    drawHeader("GPS");
    display.setCursor(0, 14);
    display.printf("Sats: %u Fix: %s\n", g_cached.gpsSats, g_cached.gpsFix ? "OK" : "NO");
    display.printf("Lat: %.5f\n", g_cached.gpsLat);
    display.printf("Lon: %.5f\n", g_cached.gpsLon);
    display.printf("Alt: %.1f m\n", g_cached.gpsAlt);
    display.printf("Age: %u ms\n", g_cached.gpsAge);
    drawFooter(5);
}
#endif

static void displayPage7() {
    display.clearDisplay();
    drawHeader("Peers");
    display.setCursor(0, 14);
    display.printf("Active: %u\n", g_cached.peerCount);
    display.printf("Last: Node%u (%u)\n", g_cached.lastMsgSrc, g_cached.lastMsgType);
    display.printf("> %s\n", g_cached.lastMsg);
    drawFooter(7);
}

static volatile bool g_buttonHandled = true;
static volatile uint32_t g_lastInterruptTime = 0;

static void IRAM_ATTR buttonISR() {
    uint32_t now = millis();
    if (now - g_lastInterruptTime > 200) {
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
    
    if (OLED_RESET_PIN != -1) {
        pinMode(OLED_RESET_PIN, OUTPUT);
        digitalWrite(OLED_RESET_PIN, LOW);
        delay(50);
        digitalWrite(OLED_RESET_PIN, HIGH);
        delay(50);
    }
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
    
    I2C_LOCK();
    Wire.begin(I2C_SDA, I2C_SCL);
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        display.ssd1306_command(SSD1306_SETCONTRAST);
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
        display.ssd1306_command(0xFF);
#else
        display.ssd1306_command(0xCF);
#endif
        display.setTextColor(SSD1306_WHITE);
        display.clearDisplay();
        display.display();
        _initState = InitState::RUNNING;
        Serial.println("  ✓ OLED Hardware initialized");
    } else {
        Serial.println("  ! OLED Hardware begin failed");
        _initState = InitState::START_I2C;
    }
    I2C_UNLOCK();
    
    g_lastUpdateTime = millis();
    g_lastActivityTime = millis();
    g_displayOn = true;
    return true;
}

void OLEDManager::_processInit() {
    uint32_t now = millis();
    switch (_initState) {
        case InitState::RESET_LOW:
            if (now - _stateStartTime >= 50) {
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
            } else if (now - _stateStartTime >= 5000) {
                Serial.println("  ! VEXT stability timeout");
                _initState = InitState::START_I2C;
            }
            break;
        case InitState::START_I2C:
            I2C_LOCK();
            if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
                display.ssd1306_command(0xFF);
#else
                display.ssd1306_command(0xCF);
#endif
                display.clearDisplay();
                display.display();
                _initState = InitState::RUNNING;
            } else {
                _stateStartTime = now;
                _initState = InitState::WAIT_POWER; 
            }
            I2C_UNLOCK();
            break;
        default: break;
    }
}

void OLEDManager::showSplash(const char* ver, const char* role) {
    I2C_LOCK();
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
    display.print("LoRaLink");
    display.setTextSize(1);
    display.setCursor(5, 52);
    display.printf("v%s | %s", ver, role);
    display.display();
    I2C_UNLOCK();
}

void OLEDManager::drawBootProgress(const char* label, int percent) {
    if (!g_displayOn) return;
    I2C_LOCK();
    display.setTextColor(SSD1306_WHITE);
    display.fillRect(2, 49, 124, 14, SSD1306_BLACK); 
    display.setCursor(5, 52);
    display.printf("%s.. %d%%", label, percent);
    int barWidth = (percent * 118) / 100;
    display.drawRect(5, 60, 118, 3, SSD1306_WHITE);
    display.fillRect(5, 60, barWidth, 3, SSD1306_WHITE);
    display.display();
    I2C_UNLOCK();
}

void OLEDManager::update() {
    uint32_t now = millis();

    // 1. Process Button Inputs (Highest Priority)
    if (!g_buttonHandled) {
        g_buttonHandled = true;
        g_lastActivityTime = now;
        
        if (_initState != InitState::RUNNING) {
            Serial.println("[OLED] Button: Attempting recovery of InitState");
            _initState = InitState::START_I2C;
            _stateStartTime = now;
        } else if (!g_displayOn) {
            setDisplayOn(true);
            Serial.println("[OLED] Wake on button");
        } else {
            g_currentPage++;
            if (g_currentPage > MAX_PAGE && g_currentPage != 7) g_currentPage = 7;
            else if (g_currentPage > 7) g_currentPage = 0;
            Serial.printf("[OLED] Page -> %u\n", g_currentPage);
        }
    }

    // 2. Handle Asynchronous Initialization
    if (_initState != InitState::RUNNING) {
        _processInit();
        if (_initState != InitState::RUNNING) return; 
    }
    if (g_displayOn && (now - g_lastActivityTime >= SLEEP_TIMEOUT_MS)) {
        setDisplayOn(false);
    }
    if (now - g_lastUpdateTime >= UPDATE_INTERVAL_MS) {
        g_lastUpdateTime = now;
        if (g_displayOn) {
            I2C_LOCK();
            switch (g_currentPage) {
                case 0: displayPage0(); break;
                case 1: displayPage1(); break;
                case 2: displayPage2(); break;
                case 3: displayPage3(); break;
                case 4: displayPage4(); break;
#ifdef HAS_GPS
                case 5: displayPage5(); break;
#endif
                case 7: displayPage7(); break;
                default: g_currentPage = 0; break;
            }
            display.display();
            I2C_UNLOCK();
        }
    }
}

void OLEDManager::setPage(uint8_t pageNum) { g_currentPage = pageNum; }
uint8_t OLEDManager::getCurrentPage() { return g_currentPage; }
void OLEDManager::setBrightness(uint8_t brightness) { g_brightness = brightness; }
uint8_t OLEDManager::getBrightness() { return g_brightness; }
void OLEDManager::setDisplayOn(bool on) { 
    if (_initState != InitState::RUNNING) return;
    g_displayOn = on; 
    I2C_LOCK();
    display.ssd1306_command(on ? 0xAF : 0xAE); 
    I2C_UNLOCK();
}
bool OLEDManager::isDisplayOn() { return g_displayOn; }
void OLEDManager::setIP(const char* ip) { if (ip) strncpy(g_cached.ip, ip, 15); }
void OLEDManager::setBatteryVoltage(float voltage, const char* mode) { 
    g_cached.batVoltage = voltage; 
    if (mode) strncpy(g_cached.powerMode, mode, 11); 
    if (voltage > 4.10) g_cached.batPercent = 100;
    else if (voltage > 3.70) g_cached.batPercent = 50 + (uint8_t)((voltage - 3.70) * 100);
    else if (voltage > 3.40) g_cached.batPercent = (uint8_t)((voltage - 3.40) * 166);
    else g_cached.batPercent = 0;
}
void OLEDManager::setBatteryPercentage(uint8_t pct) { g_cached.batPercent = pct; }
void OLEDManager::setLoRaSignal(int8_t rssi, int8_t snr) { 
    g_cached.loraRSSI = rssi; g_cached.loraSNR = (float)snr; 
    for(int i=4; i>0; i--) g_cached.rssiHistory[i] = g_cached.rssiHistory[i-1];
    g_cached.rssiHistory[0] = rssi;
}
void OLEDManager::setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora) {
    g_cached.wifiActive = wifi; g_cached.bleActive = ble; 
    g_cached.mqttActive = mqtt; g_cached.loraActive = lora;
}
void OLEDManager::setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora, bool espnow) {
    g_cached.wifiActive = wifi; g_cached.bleActive = ble; 
    g_cached.mqttActive = mqtt; g_cached.loraActive = lora;
    g_cached.espnowActive = espnow;
}
void OLEDManager::setRelayStatus(bool relayOn) {}
void OLEDManager::setTemperature(float tempC) {}
void OLEDManager::setPeerCount(uint8_t count) { g_cached.peerCount = count; }
void OLEDManager::setUptime(uint32_t uptimeMs) { g_cached.uptimeMs = uptimeMs; }
void OLEDManager::setFreeHeap(uint32_t heapBytes) { g_cached.freeHeapBytes = heapBytes; }
#ifdef HAS_GPS
void OLEDManager::setGPS(double lat, double lon, uint8_t sats, bool hasFix) { 
    g_cached.gpsLat = lat; g_cached.gpsLon = lon; g_cached.gpsSats = sats; g_cached.gpsFix = hasFix; 
}
void OLEDManager::setGPSMetrics(double alt, uint32_t age) {
    g_cached.gpsAlt = alt; g_cached.gpsAge = age;
}
#endif
void OLEDManager::setLastMessage(uint8_t src, uint8_t type, const char* msg) {
    g_cached.lastMsgSrc = src; g_cached.lastMsgType = type;
    if (msg) strncpy(g_cached.lastMsg, msg, 31);
}
void OLEDManager::setDiagnostics(uint32_t bootCount, const char* reason) { 
    g_cached.bootCount = bootCount; 
    if (reason) strncpy(g_cached.resetReason, reason, 19); 
}
void OLEDManager::setMAC(const char* mac) { if (mac) strncpy(g_cached.macSuffix, mac, 9); }
void OLEDManager::setDeviceName(const char* name) { if (name) strncpy(g_cached.deviceName, name, 19); }
void OLEDManager::setVersion(const char* ver) { if (ver) strncpy(g_cached.version, ver, 11); }
void OLEDManager::addLog(const char* msg) {}
void OLEDManager::printStatus() {}
