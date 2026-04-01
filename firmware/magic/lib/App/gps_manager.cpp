#include "gps_manager.h"
#include <Arduino.h>
#include <TinyGPS++.h>
#include "../HAL/board_config.h"
#include "ArduinoJson.h"

// ---------------------------------------------------------------------------
// Static Member Initialization
// ---------------------------------------------------------------------------

TinyGPSPlus GPSManager::_gps;
GPSManager::GPSData GPSManager::_currentData;
uint32_t GPSManager::_lastUpdate = 0;
bool GPSManager::_powerOn = false;

// ---------------------------------------------------------------------------
// Public Implementation
// ---------------------------------------------------------------------------

bool GPSManager::init() {
#if defined(GPS_RX_PIN) && defined(GPS_TX_PIN)
    Serial.printf("[GPS] Initializing on UART1 (RX:%d, TX:%d at %d baud)\n", 
                  GPS_RX_PIN, GPS_TX_PIN, GPS_SERIAL_BAUD);

    // Hardware power management (V4 spec)
    _powerEnable(true);

#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    Serial.println("[GPS] V4 Hardware: Triggering GPS Reset pulse...");
    pinMode(GPS_RST_PIN, OUTPUT);
    digitalWrite(GPS_RST_PIN, LOW);
    delay(100);
    digitalWrite(GPS_RST_PIN, HIGH);
    delay(500); // v0.4.1-4: Increased stabilization for UART sync
#endif

    // Serial1 for GPS
    Serial1.begin(GPS_SERIAL_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    return true;
#else
    Serial.println("[GPS] Pins not defined for this board variant");
    return false;
#endif
}

void GPSManager::update() {
    while (Serial1.available() > 0) {
        _gps.encode(Serial1.read());
    }

    if (_gps.location.isUpdated()) {
        _currentData.lat = _gps.location.lat();
        _currentData.lon = _gps.location.lng();
        _currentData.alt = _gps.altitude.meters();
        _currentData.hasFix = _gps.location.isValid();
        _currentData.fixAge = _gps.location.age();
        _currentData.satellites = _gps.satellites.value();
        _currentData.hdop = _gps.hdop.value();

        _lastUpdate = millis();
    }
}

GPSManager::GPSData GPSManager::getData() {
    return _currentData;
}

String GPSManager::getStatusJSON() {
    StaticJsonDocument<256> doc;
    doc["ok"] = true;
    doc["hasFix"] = _currentData.hasFix;
    doc["lat"] = _currentData.lat;
    doc["lon"] = _currentData.lon;
    doc["alt"] = _currentData.alt;
    doc["sats"] = _currentData.satellites;
    doc["age"] = _currentData.fixAge;
    doc["hdop"] = _currentData.hdop / 100.0; // TinyGPS hdop is 100ths

    String out;
    serializeJson(doc, out);
    return out;
}

String GPSManager::handleCommand(const String& args) {
    if (args == "ON") {
        _powerEnable(true);
        return "{\"ok\":true,\"msg\":\"GPS Power ON\"}";
    } else if (args == "OFF") {
        _powerEnable(false);
        return "{\"ok\":true,\"msg\":\"GPS Power OFF\"}";
    }
    return getStatusJSON();
}

// ---------------------------------------------------------------------------
// Private Implementation
// ---------------------------------------------------------------------------

void GPSManager::_powerEnable(bool on) {
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
    #ifdef GPS_EN_PIN
        pinMode(GPS_EN_PIN, OUTPUT);
        digitalWrite(GPS_EN_PIN, on ? LOW : HIGH); // Active LOW on V4
        _powerOn = on;
        Serial.printf("[GPS] Power rail → %s\n", on ? "ACTIVE" : "OFF");
    #endif
#endif
}
