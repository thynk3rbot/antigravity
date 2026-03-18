#include "GPSManager.h"
#include "DataManager.h"
#include "../utils/DebugMacros.h"

GPSManager::GPSManager() : lastPrint(0) {}

void GPSManager::init() {
#ifdef SUPPORT_GPS
  #if defined(ARDUINO_LORA_HELTEC_V4) || defined(ARDUINO_LORA_HELTEC_V3)
    // Heltec V4 has internal GNSS, V3 placeholder also uses Serial1
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    LOG_PRINTLN("GPS: Initialized Serial1 at 9600 baud.");
  #endif
  
  // Initialize DataManager GPS state
  DataManager &data = DataManager::getInstance();
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
    gps.encode(Serial1.read());
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
