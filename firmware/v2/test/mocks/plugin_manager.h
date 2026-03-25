#pragma once
#include "../arduino_stubs.h"
#include <vector>

class Plugin;
class PluginManager {
public:
    static PluginManager& getInstance() { static PluginManager inst; return inst; }
    std::vector<Plugin*> getPlugins() { return {}; }
};

class Plugin {
public:
    virtual String handleCommand(const String& cmd, const String& args) = 0;
};
