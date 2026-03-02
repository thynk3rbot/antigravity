#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h" // Heltec specific display library

// --- PIN DEFINITIONS ---
#define SDA_EXT 41
#define SCL_EXT 42
#define GPS_RX  47
#define GPS_TX  48

// --- I2C ADDRESSES ---
// KinCony uses 0x24 (Relays 1-8) and 0x25 (Relays 9-16)
#define RELAY_BANK_1 0x24
#define RELAY_BANK_2 0x25

TwoWire I2C_EXT = TwoWire(1); // Instantiate the second I2C bus
SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // Internal bus

void setup() {
  Serial.begin(115200);

  // 1. Initialize Internal Display (Bus 0)
  display.init();
  display.drawString(0, 0, "Heltec V3 Generic");
  display.display();

  // 2. Initialize External Control Array (Bus 1)
  I2C_EXT.begin(SDA_EXT, SCL_EXT, 100000);

  // 3. Initialize GPS (Serial 2)
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // Set Relay pins to high (OFF for active-low boards)
  I2C_EXT.beginTransmission(RELAY_BANK_1);
  I2C_EXT.write(0xFF);
  I2C_EXT.endTransmission();
}

void toggleRelay(uint8_t bankAddr, uint8_t relayNum, bool state) {
  // Simple bitwise manipulation for the PCF8574 expander
  static uint8_t currentStates1 = 0xFF;
  static uint8_t currentStates2 = 0xFF;

  uint8_t *states = (bankAddr == RELAY_BANK_1) ? &currentStates1 : &currentStates2;

  if (state) *states &= ~(1 << relayNum); // Active Low: 0 is ON
  else *states |= (1 << relayNum);        // Active Low: 1 is OFF

  I2C_EXT.beginTransmission(bankAddr);
  I2C_EXT.write(*states);
  I2C_EXT.endTransmission();
}

void loop() {
  // Example: Sequential toggle
  for(int i=0; i<8; i++) {
    toggleRelay(RELAY_BANK_1, i, true);
    delay(200);
    toggleRelay(RELAY_BANK_1, i, false);
  }
}
