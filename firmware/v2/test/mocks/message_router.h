#pragma once
#include <string>
#include "../arduino_stubs.h"

class MessageRouter {
public:
    static MessageRouter& getInstance() { static MessageRouter inst; return inst; }
    void routeCommand(const String&, const String&) {}
};
