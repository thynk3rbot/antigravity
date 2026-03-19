#include "heartbeat.h"

// Static member definitions
Heartbeat::HeartbeatCallback Heartbeat::_callback = nullptr;
uint32_t Heartbeat::_lastBeat = 0;
uint32_t Heartbeat::_count    = 0;

void Heartbeat::begin(HeartbeatCallback cb) {
    _callback  = cb;
    _lastBeat  = millis();
    _count     = 0;
}

void Heartbeat::tick() {
    if (!_callback) return;

    uint32_t now      = millis();
    uint32_t interval = PowerManager::getHeartbeatIntervalMs();

    if ((now - _lastBeat) >= interval) {
        _lastBeat = now;
        _count++;
        _callback();
    }
}

uint32_t Heartbeat::getCount() {
    return _count;
}

uint32_t Heartbeat::getLastBeatMs() {
    return _lastBeat;
}
