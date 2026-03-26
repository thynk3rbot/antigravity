/*
 * LoRaLink V4 Diagnostic Sketch
 * Tests: OLED display, LoRa radio init, WiFi scan, button
 * Uses: Adafruit SSD1306 + RadioLib (same as v2 project)
 */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <WiFi.h>

// V4 pin definitions
#define OLED_SDA    17
#define OLED_SCL    18
#define OLED_RST    21
#define OLED_ADDR   0x3C
#define VEXT_PIN    36   // Active HIGH on V4
#define BUTTON_PIN  0

// LoRa SX1262 pins (V4)
#define LORA_NSS    8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

int testLine = 0;

void oledPrint(const char* msg, bool clearFirst = false) {
  if (clearFirst) { display.clearDisplay(); testLine = 0; }
  display.setCursor(0, testLine * 8);
  display.println(msg);
  display.display();
  Serial.println(msg);
  testLine++;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Power on VEXT (V4 = Active HIGH)
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, HIGH);
  delay(200);

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // I2C + OLED
  Wire.begin(OLED_SDA, OLED_SCL, 100000);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    oledPrint("V4 DIAG v0.0.1", true);
    oledPrint("OLED: OK");
  } else {
    Serial.println("OLED FAIL");
  }

  // LoRa
  SPI.begin(9, 11, 10, LORA_NSS);  // V4: SCK=9, MISO=11, MOSI=10
  int loraState = radio.begin(915.0);
  if (loraState == RADIOLIB_ERR_NONE) {
    oledPrint("LoRa: OK");
  } else {
    char buf[24];
    snprintf(buf, sizeof(buf), "LoRa ERR: %d", loraState);
    oledPrint(buf);
  }

  // WiFi scan
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  char wfbuf[24];
  snprintf(wfbuf, sizeof(wfbuf), "WiFi: %d APs", n);
  oledPrint(wfbuf);

  // Chip info
  char chipbuf[24];
  snprintf(chipbuf, sizeof(chipbuf), "MAC: %s", WiFi.macAddress().substring(9).c_str());
  oledPrint(chipbuf);

  oledPrint("Press BTN->next");
}

int page = 0;
bool lastBtn = HIGH;

void loop() {
  bool btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && lastBtn == HIGH) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      page = (page + 1) % 3;
      display.clearDisplay();
      testLine = 0;
      switch (page) {
        case 0:
          oledPrint("=== RADIO TX ===", true);
          {
            int state = radio.transmit("PING");
            oledPrint(state == RADIOLIB_ERR_NONE ? "TX: OK" : "TX: FAIL");
          }
          break;
        case 1:
          oledPrint("=== HEAP ===", true);
          {
            char buf[24];
            snprintf(buf, sizeof(buf), "Free: %dKB", ESP.getFreeHeap()/1024);
            oledPrint(buf);
            snprintf(buf, sizeof(buf), "PSRAM: %dKB", ESP.getFreePsram()/1024);
            oledPrint(buf);
          }
          break;
        case 2:
          oledPrint("=== INFO ===", true);
          oledPrint(WiFi.macAddress().c_str());
          {
            char buf[24];
            snprintf(buf, sizeof(buf), "CPU: %dMHz", ESP.getCpuFreqMHz());
            oledPrint(buf);
          }
          break;
      }
    }
  }
  lastBtn = btn;
  delay(20);
}