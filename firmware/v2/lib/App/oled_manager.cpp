#include "oled_manager.h"
#include "board_config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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
    bool relayOn;
    double gpsLat;
    double gpsLon;
    uint8_t gpsSats;
    bool gpsFix;
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
static bool g_buttonPressed = false;
static uint32_t g_buttonPressTime = 0;

static const uint32_t UPDATE_INTERVAL_MS = 500;
static const uint32_t SLEEP_TIMEOUT_MS = 30000;
// Using BUTTON_DEBOUNCE_MS from board_config.h
// Using OLED_AUTO_ROTATE_MS from board_config.h
#define AUTO_ROTATE_MS OLED_AUTO_ROTATE_MS

static void displayPage1();
static void displayPage2();
static void displayPage3();
static void displayPage4();
static void displayPage5();
static void displayPage6();

// ============================================================================
// Internal Helpers
// ============================================================================

static void drawHeader(const char* title) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("%s | %s", g_cached.deviceName, title);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

static void drawFooter(uint8_t pageNum) {
    display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    display.setCursor(0, 56);
    display.printf("v%s | Page %u/6", g_cached.version, pageNum);
}

static void displayPage1() {
    display.clearDisplay();
    drawHeader("Network");
    display.setCursor(0, 14);
    display.printf("IP:    %s\n", g_cached.ip);
    display.printf("Peers: %u connected\n", g_cached.peerCount);
    display.printf("WiFi:  %s\n", g_cached.wifiActive ? "ACTIVE" : "OFF");
    drawFooter(1);
    display.display();
}

static void displayPage2() {
    display.clearDisplay();
    drawHeader("Power");
    display.setCursor(0, 14);
    display.printf("Batt: %.2f V\n", g_cached.batVoltage);
    display.printf("RSSI: %d dBm\n", g_cached.loraRSSI);
    display.printf("SNR:  %.1f dB\n", g_cached.loraSNR);
    drawFooter(2);
    display.display();
}

static void displayPage3() {
    display.clearDisplay();
    drawHeader("Transports");
    display.setCursor(0, 14);
    display.printf("BLE:   %s\n", g_cached.bleActive ? "ON" : "OFF");
    display.printf("MQTT:  %s\n", g_cached.mqttActive ? "ON" : "OFF");
    display.printf("LoRa:  %s\n", g_cached.loraActive ? "ON" : "OFF");
    display.printf("ESPNW: %s\n", g_cached.espnowActive ? "ON" : "OFF");
    drawFooter(3);
    display.display();
}

static void displayPage4() {
    display.clearDisplay();
    drawHeader("System");
    display.setCursor(0, 14);
    display.printf("Temp:  %.1f C\n", g_cached.tempC);
    display.printf("Heap:  %u KB\n", g_cached.freeHeapBytes / 1024);
    display.printf("MAC:   %s\n", g_cached.macSuffix);
    drawFooter(4);
    display.display();
}

static void displayPage5() {
    display.clearDisplay();
    drawHeader("GNSS/GPS");
    display.setCursor(0, 14);
    if (!g_cached.gpsFix && g_cached.gpsSats == 0) {
        display.println("Searching...");
    } else {
        display.printf("Sats: %u Lock: %s\n", g_cached.gpsSats, g_cached.gpsFix ? "YES" : "NO");
        display.printf("Lat: %.6f\n", g_cached.gpsLat);
        display.printf("Lon: %.6f\n", g_cached.gpsLon);
    }
    drawFooter(5);
    display.display();
}

static void displayPage6() {
    display.clearDisplay();
    drawHeader("Diagnostics");
    display.setCursor(0, 14);
    display.printf("Boot Count: %u\n", g_cached.bootCount);
    display.printf("Reset: %s\n", g_cached.resetReason);
    display.printf("Uptime: %u min", g_cached.uptimeMs / 60000);
    drawFooter(6);
    display.display();
}

static void IRAM_ATTR buttonISRHandler() {
    uint32_t now = millis();
    if (digitalRead(BUTTON_PIN) == LOW) {
        if (!g_buttonPressed) {
            g_buttonPressTime = now;
            g_buttonPressed = true;
        }
    } else {
        if (g_buttonPressed) {
            uint32_t pressDuration = now - g_buttonPressTime;
            if (pressDuration >= BUTTON_DEBOUNCE_MS) {
                g_lastActivityTime = now;
                if (!g_displayOn) {
                    g_wakeRequested = true;
                } else {
                    g_currentPage = (g_currentPage + 1) % 6;
                    g_lastAutoRotateTime = now;
                }
            }
            g_buttonPressed = false;
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

OLEDManager::OLEDManager() {
    memset(&g_cached, 0, sizeof(g_cached));
}

bool OLEDManager::init() {
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    if (OLED_RESET_PIN != -1) {
        pinMode(OLED_RESET_PIN, OUTPUT);
        digitalWrite(OLED_RESET_PIN, LOW); delay(20);
        digitalWrite(OLED_RESET_PIN, HIGH); delay(20);
    }
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) return false;
    
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(0xFF);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISRHandler, CHANGE);
    
    g_lastUpdateTime = millis();
    g_lastActivityTime = millis();
    g_lastAutoRotateTime = millis();
    g_displayOn = true;
    return true;
}

void OLEDManager::showSplash(const char* ver, const char* role) {
    display.clearDisplay();
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
}

void OLEDManager::drawBootProgress(const char* label, int percent) {
    if (!g_displayOn) return;
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
    uint32_t now = millis();
    if (g_wakeRequested) { setDisplayOn(true); g_wakeRequested = false; }
    if (g_displayOn && (now - g_lastActivityTime >= SLEEP_TIMEOUT_MS)) setDisplayOn(false);
    
    if (g_displayOn && (now - g_lastAutoRotateTime >= AUTO_ROTATE_MS)) {
        g_currentPage = (g_currentPage + 1) % 6;
        g_lastAutoRotateTime = now;
    }

    if (now - g_lastUpdateTime >= UPDATE_INTERVAL_MS) {
        g_lastUpdateTime = now;
        if (g_displayOn) {
            switch (g_currentPage) {
                case 0: displayPage1(); break;
                case 1: displayPage2(); break;
                case 2: displayPage3(); break;
                case 3: displayPage4(); break;
                case 4: displayPage5(); break;
                case 5: displayPage6(); break;
                default: g_currentPage = 0; break;
            }
        }
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

void OLEDManager::setRelayStatus(bool relayOn) { g_cached.relayOn = relayOn; }
void OLEDManager::setTemperature(float tempC) { g_cached.tempC = tempC; }
void OLEDManager::setPeerCount(uint8_t count) { g_cached.peerCount = count; }
void OLEDManager::setUptime(uint32_t uptimeMs) { g_cached.uptimeMs = uptimeMs; }
void OLEDManager::setFreeHeap(uint32_t heapBytes) { g_cached.freeHeapBytes = heapBytes; }
void OLEDManager::setGPS(double lat, double lon, uint8_t sats, bool hasFix) { g_cached.gpsLat = lat; g_cached.gpsLon = lon; g_cached.gpsSats = sats; g_cached.gpsFix = hasFix; }
void OLEDManager::setDiagnostics(uint32_t bootCount, const char* reason) { g_cached.bootCount = bootCount; if (reason) strncpy(g_cached.resetReason, reason, 19); }
void OLEDManager::setMAC(const char* mac) { if (mac) strncpy(g_cached.macSuffix, mac, 9); }
void OLEDManager::setDeviceName(const char* name) { if (name) strncpy(g_cached.deviceName, name, 19); }
void OLEDManager::setVersion(const char* ver) { if (ver) strncpy(g_cached.version, ver, 11); }
void OLEDManager::addLog(const char* msg) {}
void OLEDManager::printStatus() {}
