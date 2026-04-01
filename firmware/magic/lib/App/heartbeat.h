#pragma once
#include <Arduino.h>
#include <functional>
#include "power_manager.h"

class Heartbeat {
public:
    using HeartbeatCallback = std::function<void()>;

    static void begin(HeartbeatCallback cb);
    static void tick();           // Call in main loop
    static uint32_t getCount();
    static uint32_t getLastBeatMs();

private:
    static HeartbeatCallback _callback;
    static uint32_t _lastBeat;
    static uint32_t _count;
};
