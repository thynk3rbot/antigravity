/**
 * @file plugin_manager.h
 * @brief Standardized Plugin Infrastructure for LoRaLink
 */

#pragma once

#include <stdint.h>
#include <vector>
#include "hal_compat.h"

/**
 * @class Plugin
 * @brief Base class for all system extensions (sensors, handlers, etc.)
 */
class Plugin {
public:
    virtual ~Plugin() = default;

    /** @brief Called during system setup() */
    virtual bool init() = 0;

    /** @brief Called periodically in the controlTask loop */
    virtual void poll() = 0;

    /** @brief Unique name for the plugin (for logs/registry) */
    virtual const char* getName() const = 0;
};

/**
 * @class PluginManager
 * @brief Central registry and lifecycle manager for plugins
 */
class PluginManager {
public:
    static PluginManager& getInstance();

    /** @brief Register a plugin (usually in setup() before initAll()) */
    void registerPlugin(Plugin* plugin);

    /** @brief Initialize all registered plugins */
    void initAll();

    /** @brief Poll all active plugins */
    void pollAll();

    /** @brief Get the full list of registered plugins */
    const std::vector<Plugin*>& getPlugins() const { return _plugins; }

private:
    PluginManager() = default;
    std::vector<Plugin*> _plugins;
};

extern PluginManager& pluginManager;
