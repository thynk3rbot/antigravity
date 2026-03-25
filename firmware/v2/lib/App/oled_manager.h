#pragma once

#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>

class OLEDManager {
public:
    static OLEDManager& getInstance() {
        static OLEDManager instance;
        return instance;
    }

    bool init();
    void update();
    void drawBootProgress(const char* label, int percent);
    void showSplash(const char* ver, const char* role);
    void setPage(uint8_t pageNum);
    uint8_t getCurrentPage();
    void setBrightness(uint8_t brightness);
    uint8_t getBrightness();
    void setDisplayOn(bool on);
    bool isDisplayOn();

    void setIP(const char* ip);
    void setBatteryVoltage(float voltage, const char* mode);
    void setBatteryPercentage(uint8_t pct);
    void setLoRaSignal(int8_t rssi, int8_t snr);
    void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora);
    void setTransportStatus(bool wifi, bool ble, bool mqtt, bool lora, bool espnow);
    void setRelayStatus(bool relayOn);
    void setTemperature(float tempC);
    void setPeerCount(uint8_t count);
    void setUptime(uint32_t uptimeMs);
    void setFreeHeap(uint32_t heapBytes);
    void setGPS(double lat, double lon, uint8_t sats, bool hasFix);
    void setGPSMetrics(double alt, uint32_t age);
    void setLastMessage(uint8_t src, uint8_t type, const char* msg);
    void setDiagnostics(uint32_t bootCount, const char* reason);
    void setMAC(const char* mac);
    void setDeviceName(const char* name);
    void setVersion(const char* ver);
    void addLog(const char* msg);
    void printStatus();

    enum class InitState : uint8_t {
        IDLE            = 0,
        RESET_LOW       = 1,
        RESET_HIGH      = 2,
        WAIT_POWER      = 3,
        START_I2C       = 4,
        RUNNING         = 255
    };

    bool isReady() const { return _initState == InitState::RUNNING; }

private:
    static InitState _initState;
    static uint32_t _stateStartTime;
    static void _processInit();
    OLEDManager();
};
