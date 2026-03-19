#include "GPSManager.h"
#include "DataManager.h"
#include "../utils/DebugMacros.h"

GPSManager::GPSManager() : lastPrint(0), rawMode(false) {}

void GPSManager::init() {
#ifdef SUPPORT_GPS
  DataManager &data = DataManager::getInstance();
  uint8_t rx = data.getPinGpsRx();
  uint8_t tx = data.getPinGpsTx();
  
  // Baud rate: use configured value (defaults: V4 internal=115200, external=9600)
  uint32_t baud = data.gpsBaud;

  #if defined(ARDUINO_LORA_HELTEC_V4)
    // Heltec V4 UC6580 GNSS Power Management
    pinMode(PIN_GPS_RST, OUTPUT);
    pinMode(PIN_GPS_WAKE, OUTPUT);
    pinMode(PIN_GPS_PPS, INPUT);
    
    digitalWrite(PIN_GPS_RST, LOW);  // Reset Active LOW
    delay(100);
    digitalWrite(PIN_GPS_RST, HIGH); 
    digitalWrite(PIN_GPS_WAKE, HIGH); // Pull Wake HIGH to start GNSS engine
    LOG_PRINTLN("GPS: V4 GNSS (UC6580) Power Rails Initialized.");
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
    LOG_PRINTLN("GPS: Raw NMEA streaming to Serial enabled.");
  } else {
    LOG_PRINTLN("GPS: Raw NMEA streaming disabled.");
  }
}

void GPSManager::resetGNSS() {
#if defined(ARDUINO_LORA_HELTEC_V4)
  LOG_PRINTLN("GPS: Hardware Reset GNSS (Active LOW pulse)...");
  digitalWrite(PIN_GPS_RST, LOW);
  delay(100);
  digitalWrite(PIN_GPS_RST, HIGH);
  LOG_PRINTLN("GPS: Reset complete.");
#else
  LOG_PRINTLN("GPS: Reset not supported on this hardware (Software only).");
#endif
}

String GPSManager::getDiagnostic() {
  DataManager &data = DataManager::getInstance();
  String out = "GNSS DIAGNOSTIC:\n";
  out += "  Baud: " + String(data.gpsBaud) + "\n";
#if defined(ARDUINO_LORA_HELTEC_V4)
  out += "  Pins: RST=" + String(PIN_GPS_RST) + " WAKE=" + String(PIN_GPS_WAKE) + " PPS=" + String(PIN_GPS_PPS) + "\n";
  out += "  States: RST=" + String(digitalRead(PIN_GPS_RST)) + " WAKE=" + String(digitalRead(PIN_GPS_WAKE)) + " PPS=" + String(digitalRead(PIN_GPS_PPS)) + "\n";
#endif
  out += "  TinyGPS++: Chars=" + String(gps.charsProcessed()) + " SentencesWithFix=" + String(gps.sentencesWithFix()) + " FailedChecksum=" + String(gps.failedChecksum()) + "\n";
  out += "  Satellites: " + String(gps.satellites.value()) + "\n";
  out += "  Fix Age: " + String(gps.location.age()) + " ms\n";
  return out;
}

