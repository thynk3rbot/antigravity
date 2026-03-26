# Internal: Plugin Development Guide

This guide describes how to add a new hardware or logic module to the LoRaLink core as a `Plugin`.

## 1. Lifecycle of a Plugin

All plugins must inherit from the `Plugin` base class in `lib/App/plugin_manager.h`:
```cpp
class MyPlugin : public Plugin {
public:
    virtual bool init() override;         // Initialize hardware/states
    virtual void poll() override;         // Periodic tasks (10Hz)
    virtual void configure(JsonObjectConst config) override; // Apply JSON configuration
    virtual const char* getName() const override { return "MyPlugin"; }
};
```

### Methods
- **`init()`**: Called during system setup. Should perform one-time hardware setup.
- **`poll()`**: Called in the `controlTask` loop. Use for sensor sampling or state updates.
- **`configure()`**: Called when a "Product" is loaded. This is where you map pins from JSON.

## 2. Dynamic Configuration
The `JsonObjectConst config` parameter in `configure()` contains the `config` block from the product manifest. Use it to override default pins:

```cpp
void MyPlugin::configure(JsonObjectConst config) {
    if (config.containsKey("sensor_pin")) {
        _pin = config["sensor_pin"];
    }
    
    // Board-specific selection
    String hw = HW_VERSION; // Defined in board_config.h
    if (config.containsKey(hw)) {
        _pin = config[hw]["sensor_pin"];
    }
}
```

## 3. Registration
New plugins must be registered in `src/main.cpp` before they can be activated via JSON:

```cpp
// In setup()
static MyPlugin myPlugin;
PluginManager::getInstance().registerPlugin(&myPlugin);
```

## 4. Best Practices
- **Non-Blocking**: Never use `delay()` in `init()` or `poll()`. Use `millis()` based state machines or FreeRTOS timers.
- **Centralized Config**: Always use `configure()` for pins instead of hardcoding.
- **MQTT/Mesh**: If your plugin needs MQTT topics, use `MQTTTransport::instance()->registerTopic()`.
