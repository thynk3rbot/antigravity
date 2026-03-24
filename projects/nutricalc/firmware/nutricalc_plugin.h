#ifndef NUTRICALC_PLUGIN_H
#define NUTRICALC_PLUGIN_H

#include "../App/plugin_manager.h"
#include <ArduinoJson.h>

class NutriCalcPlugin : public Plugin {
public:
    NutriCalcPlugin();
    
    bool init() override;
    void poll() override;
    void configure(JsonObjectConst config) override;
    String handleCommand(const String& cmd, const String& args) override;
    const char* getName() const override { return "NutriCalc"; }

private:
    void _handlePump(int pumpId, float ml, float grams);
    int _pumpPins[3] = {33, 25, 100}; // Default V2 pins
};

#endif // NUTRICALC_PLUGIN_H
