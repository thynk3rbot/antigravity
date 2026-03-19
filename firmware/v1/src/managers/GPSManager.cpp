#include "GPSManager.h"
#include "DataManager.h"
#include "../utils/DebugMacros.h"

GPSManager::GPSManager() : lastPrint(0), rawMode(false) {}

void GPSManager::init() {
#ifdef SUPPORT_GPS
  DataManager &data = DataManager::getInstance();
  uint8_t rx = data.getPinGpsRx();
  uint8_t tx = data.getPinGpsTx();
  uint32_t baud = data.gpsBaud;

  #if defined(ARDUINO_LORA_HELTEC_V4)
    // 1. GNSS Power Enable (GPIO 34 from hardware pin map)
    pinMode(PIN_GPS_GNSS_EN, OUTPUT);
    digitalWrite(PIN_GPS_GNSS_EN, LOW); // Active LOW to enable UC6580 power
    delay(200);

    // 2. GNSS Reset & Wake
    pinMode(PIN_GPS_RST, OUTPUT);
    pinMode(PIN_GPS_WAKE, OUTPUT);
    
    // Hardware Reset Pulse - UC6580 needs a solid reset
    digitalWrite(PIN_GPS_RST, LOW);
    delay(250);
    digitalWrite(PIN_GPS_RST, HIGH);
    delay(500); // Allow internal LDOs to stabilize

    // Wake Pulse - Trigger cold start if needed
    digitalWrite(PIN_GPS_WAKE, LOW);
    delay(100);
    digitalWrite(PIN_GPS_WAKE, HIGH);
    delay(200);
    
    LOG_PRINTLN("GPS: V4 GNSS (GPIO 34) Power Rail Enabled.");
  #endif
  
  Serial1.begin(baud, SERIAL_8N1, rx, tx);
  LOG_PRINTF("GPS: Initialized at %u baud (RX:%d, TX:%d)\n", baud, rx, tx);

  // Initialize DataManager GPS state
  data.gpsLat = 0.0;
  data.gpsLon = 0.0;
  data.gpsAlt = 0.0;
  data.gpsSats = 0;
  data.gpsFixed = false;
  data.gpsLastFixAge = 0;
#endif
}

void GPSManager::loop() {
#ifdef SUPPORT_GPS
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (rawMode) {
      Serial.write(c);
    }
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    DataManager &data = DataManager::getInstance();
    data.gpsLat = gps.location.lat();
    data.gpsLon = gps.location.lng();
    data.gpsAlt = gps.altitude.meters();
    data.gpsSats = gps.satellites.value();
    data.gpsFixed = gps.location.isValid();
    data.gpsLastFixAge = gps.location.age();

    if (millis() - lastPrint > 10000) {
      lastPrint = millis();
      LOG_PRINTF("GPS: Lat=%f, Lon=%f, Sats=%d, Fix=%s\n", 
                 data.gpsLat, data.gpsLon, data.gpsSats, 
                 data.gpsFixed ? "YES" : "NO");
    }
  }
#endif
}

void GPSManager::setRawMode(bool enabled) {
  rawMode = enabled;
  if (enabled) {
    LOG_PRINTLN("GPS: Raw NMEA streaming enabled.");
  } else {
    LOG_PRINTLN("GPS: Raw NMEA streaming disabled.");
  }
}

void GPSManager::resetGNSS() {
#if defined(ARDUINO_LORA_HELTEC_V4)
  LOG_PRINTLN("GPS: Hardware Reset Pulse...");
  digitalWrite(PIN_GPS_RST, LOW);
  delay(100);
  digitalWrite(PIN_GPS_RST, HIGH);
#endif
}

String GPSManager::getDiagnostic() {
  DataManager &data = DataManager::getInstance();
  String out = "GNSS DIAGNOSTIC:\n";
  out += "  Baud: " + String(data.gpsBaud) + "\n";
#if defined(ARDUINO_LORA_HELTEC_V4)
  out += "  Pins: PWR=34 RX=" + String(PIN_GPS_RX) + " TX=" + String(PIN_GPS_TX) + "\n";
#endif
  out += "  TinyGPS++: Chars=" + String(gps.charsProcessed()) + " FixSentences=" + String(gps.sentencesWithFix()) + "\n";
  out += "  Satellites: " + String(gps.satellites.value()) + "\n";
  return out;
}
