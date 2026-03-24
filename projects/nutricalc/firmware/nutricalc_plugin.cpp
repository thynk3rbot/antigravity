#include "nutricalc_plugin.h"
#include "../App/schedule_manager.h"
#include "../Transport/mqtt_transport.h"
#include <Arduino.h>

NutriCalcPlugin::NutriCalcPlugin() {}

bool NutriCalcPlugin::init() {
    Serial.println("[NutriCalc] Plugin initialized.");
    
    // Register MQTT topics for uncoupling
    MQTTTransport::instance()->registerTopic("nutricalc/pump/#", [this](const String& topic, const String& payload) {
        this->handleCommand("NUTRICALC", topic + "|" + payload);
    });
    
    MQTTTransport::instance()->registerTopic("nutricalc/dose", [this](const String& topic, const String& payload) {
        this->handleCommand("NUTRICALC", topic + "|" + payload);
    });
    
    return true;
}

void NutriCalcPlugin::poll() {
    // No background polling needed for NutriCalc commands
}

void NutriCalcPlugin::configure(JsonObjectConst config) {
    // 1. Check for flat config
    if (config.containsKey("pump1")) _pumpPins[0] = config["pump1"];
    if (config.containsKey("pump2")) _pumpPins[1] = config["pump2"];
    if (config.containsKey("pump3")) _pumpPins[2] = config["pump3"];
    
    // 2. Check for board-specific overrides (V2/V3/V4)
    String hw = HW_VERSION;
    if (config.containsKey(hw)) {
        JsonObjectConst hwConfig = config[hw];
        if (hwConfig.containsKey("pump1")) _pumpPins[0] = hwConfig["pump1"];
        if (hwConfig.containsKey("pump2")) _pumpPins[1] = hwConfig["pump2"];
        if (hwConfig.containsKey("pump3")) _pumpPins[2] = hwConfig["pump3"];
    }

    Serial.printf("[NutriCalc] Configured for %s: Pins [%d, %d, %d]\n", 
                  hw.c_str(), _pumpPins[0], _pumpPins[1], _pumpPins[2]);
}

String NutriCalcPlugin::handleCommand(const String& cmd, const String& args) {
    if (cmd == "NUTRIC" || cmd == "NUTRICALC") {
        // Parse the args as the legacy 'input' format: "topic|payload"
        int pipeIdx = args.indexOf('|');
        if (pipeIdx < 0) return "{\"ok\":false,\"error\":\"Format: NUTRIC topic|json\"}";

        String topic = args.substring(0, pipeIdx);
        String payload = args.substring(pipeIdx + 1);

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) return "{\"ok\":false,\"error\":\"JSON parse failed\"}";

        if (topic.startsWith("nutricalc/pump/")) {
            int pumpId = topic.substring(15).toInt();
            float grams = doc["grams"] | 0.0f;
            float ml = doc["ml"] | 0.0f;
            _handlePump(pumpId, ml, grams);
            return "{\"ok\":true,\"pump\":" + String(pumpId) + ",\"msg\":\"Queued\"}";
        }
    }
    return ""; // Not handled
}

void NutriCalcPlugin::_handlePump(int pumpId, float ml, float grams) {
    // Calibration: 1ml = 1000ms (assumed for generic peristaltic pump)
    unsigned long durationMs = (unsigned long)(ml * 1000.0f);
    if (durationMs == 0 && grams > 0) {
        durationMs = (unsigned long)(grams * 1200.0f); // Fallback for grams
    }

    if (durationMs > 0 && pumpId >= 1 && pumpId <= 3) {
        int pin = _pumpPins[pumpId - 1];
        if (pin != -1 && pin < 255) {
            ScheduleManager::addTask("Pump" + String(pumpId), "PULSE", pin, 0, durationMs / 1000, "NUTRI");
            Serial.printf("[NutriCalc] Pump %d on pin %d for %lu ms\n", pumpId, pin, durationMs);
        }
    }
}
