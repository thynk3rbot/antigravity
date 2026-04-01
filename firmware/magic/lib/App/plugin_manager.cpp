/**
 * @file plugin_manager.cpp
 * @brief Implementation of the Plugin Infrastructure
 */

#include "plugin_manager.h"
#include "nvs_manager.h"
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
        const char* name = (*it)->getName();
        
        // Check if feature is globally disabled in NVS
        if (!isEnabled(name)) {
            Serial.printf("  - Plugin '%s' is DISABLED in registry - skipping\n", name);
            it = _plugins.erase(it);
            continue;
        }

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

bool PluginManager::isEnabled(const char* featureName) {
    if (!featureName) return true;
    return NVSManager::isFeatureEnabled(std::string(featureName), true);
}
