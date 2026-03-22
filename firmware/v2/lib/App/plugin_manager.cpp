/**
 * @file plugin_manager.cpp
 * @brief Implementation of the Plugin Infrastructure
 */

#include "plugin_manager.h"
#include "hal_compat.h"

PluginManager& pluginManager = PluginManager::getInstance();

PluginManager& PluginManager::getInstance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::registerPlugin(Plugin* plugin) {
    if (plugin) {
        _plugins.push_back(plugin);
    }
}

void PluginManager::initAll() {
    Serial.println("\n[PluginMgr] Initializing all plugins...");
    auto it = _plugins.begin();
    while (it != _plugins.end()) {
        if ((*it)->init()) {
            Serial.printf("  ✓ Plugin '%s' ready\n", (*it)->getName());
            ++it;
        } else {
            Serial.printf("  ! Plugin '%s' failed to initialize - removed\n", (*it)->getName());
            it = _plugins.erase(it);
        }
    }
}

void PluginManager::pollAll() {
    for (auto* plugin : _plugins) {
        plugin->poll();
    }
}
