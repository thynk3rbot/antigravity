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

  void init();
  void loop();

private:
  GPSManager();
  TinyGPSPlus gps;
  unsigned long lastPrint;
};

#endif // GPS_MANAGER_H
