#include <Arduino.h>

// =============================================================================
//  LoRaLink Xiao Satellite — Serial Sensor Bridge
//  Target: Seeeduino XIAO SAMD21 (or any Arduino-compatible)
//  Connects to Heltec LoRa V3 via UART at 115200 baud
//  (c) 2026 Steven P Williams (spw1.com)
// =============================================================================

#define XIAO_FW_VERSION "v0.1.0"
#define XIAO_BOARD "Seeeduino XIAO SAMD21"
#define SENSOR_INTERVAL 5000 // ms between telemetry broadcasts

// Forward Declarations
void sendHello();
void sendTelemetry();
void handleCommand(const String &cmd);

// ─── Sensor Includes ────────────────────────────────────────────────────────
// Uncomment the sensors you have wired up:
// #include <DHT.h>         // Temperature / Humidity (DHT11/22)
// #include <Wire.h>        // I2C sensors (BMP280, SHT31, etc)
// #include <Adafruit_BMP280.h>

// ─── Pin Definitions (XIAO SAMD21) ──────────────────────────────────────────
#define PIN_SENSOR_A0 A0 // Analog sensor (light, pot, etc)
#define PIN_SENSOR_A1 A1 // Second analog channel
// #define PIN_DHT      D3
// #define DHTTYPE      DHT22

// ─── State ───────────────────────────────────────────────────────────────────
unsigned long lastTelemetry = 0;
bool helloDone = false;

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);  // USB Debug
  Serial1.begin(115200); // Hardware UART (Pins D6/D7)
  while (!Serial && millis() < 3000)
    ; // Wait up to 3s for USB debug if connected

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // OFF (active low)

  // Brief blink to confirm boot
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);
  digitalWrite(LED_BUILTIN, HIGH);
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
  // Send HELLO once after 2s (gives Heltec time to boot serial task)
  if (!helloDone && millis() > 2000) {
    sendHello();
    helloDone = true;
  }

  // Periodic sensor telemetry
  if (millis() - lastTelemetry >= SENSOR_INTERVAL) {
    lastTelemetry = millis();
    sendTelemetry();
  }

  // Listen for commands from both USB and Hardware UART
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    handleCommand(cmd);
  }
  if (Serial1.available()) {
    String cmd = Serial1.readStringUntil('\n');
    cmd.trim();
    handleCommand(cmd);
  }
}

// ─── Announcements ───────────────────────────────────────────────────────────
void sendHello() {
  String hello = "HELLO device=" + String(XIAO_BOARD) +
                 " hw=SAMD21"
                 " fw=" +
                 String(XIAO_FW_VERSION) + " caps=adc0,adc1,uptime";
  Serial.println(hello);
  Serial1.println(hello);
}

// ─── Telemetry ───────────────────────────────────────────────────────────────
void sendTelemetry() {
  // Read sensors
  int raw0 = analogRead(PIN_SENSOR_A0);
  int raw1 = analogRead(PIN_SENSOR_A1);
  float volts0 = raw0 * (3.3f / 1023.0f);
  float volts1 = raw1 * (3.3f / 1023.0f);
  unsigned long uptime = millis() / 1000;

  // Send structured SENSOR line
  String sensor = "SENSOR adc0=" + String(volts0, 3) +
                  " adc1=" + String(volts1, 3) + " uptime=" + String(uptime);
  Serial.println(sensor);
  Serial1.println(sensor);

  // Blink LED on transmit
  digitalWrite(LED_BUILTIN, LOW);
  delay(20);
  digitalWrite(LED_BUILTIN, HIGH);
}

// ─── Command Handling
// ─────────────────────────────────────────────────────────
void handleCommand(const String &cmd) {
  if (cmd.startsWith("PING")) {
    String resp = "SENSOR status=ok fw=" + String(XIAO_FW_VERSION);
    Serial.println(resp);
    Serial1.println(resp);
  } else if (cmd.startsWith("HELLO")) {
    // Heltec re-requesting hello (e.g. after its own reboot)
    sendHello();
    helloDone = true;
  }
  // Add more command handlers here as capabilities grow
}
