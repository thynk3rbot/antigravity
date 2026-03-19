#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <TinyGPS++.h>

class GPSManager {
public:
  static GPSManager &getInstance() {
    static GPSManager instance;
    return instance;
  }

  TinyGPSPlus gps;

  void init();
  void loop();
  void setRawMode(bool enabled);
  bool getRawMode() const { return rawMode; }
  void resetGNSS();
  String getDiagnostic();
  
  uint32_t getCharsProcessed() const { return gps.charsProcessed(); }
  uint32_t getSentencesWithFix() const { return gps.sentencesWithFix(); }
  uint32_t getFailedChecksum() const { return gps.failedChecksum(); }


private:
  GPSManager();
  unsigned long lastPrint;
  bool rawMode;
};

#endif // GPS_MANAGER_H
