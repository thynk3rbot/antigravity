/**
 * @file sim_runner.cpp
 * @brief Standalone simulation runner for PC (native)
 */

#ifdef NATIVE_TEST
#include "hal_compat.h"
#include "plugin_manager.h"
#include "../HAL/sim_hal.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

// Define the global MockSerial instance
MockSerial Serial;

// --- Simulated Hardware ---
SimDigitalIO pumpRelay(25);      // Virtual pin 25
SimAnalogIn tempSensor(34);     // Virtual pin 34

void setup_sim() {
    Serial.println("=== LoRaLink Local Process Simulator ===");
    
    // Initialize Plugins (if any registered)
    pluginManager.initAll();
    
    Serial.println("Simulator Ready.");
}

void loop_sim() {
    static uint32_t lastTick = 0;
    static uint32_t lastAiQuery = 0;
    uint32_t now = millis();
    
    // 10Hz simulation tick
    if (now - lastTick >= 100) {
        pluginManager.pollAll();
        
        // Output state for Python bridge (JSON format)
        // Must be single-line JSON to be picked up by SimulatorBridge
        printf("{\"uptime\": %u, \"pins\": [{\"id\": 25, \"val\": %d}, {\"id\": 34, \"val\": %u}]}\n", 
               now, pumpRelay.read(), tempSensor.readRaw());
        fflush(stdout);
        
        // Periodic AI Query test every 15 seconds
        if (now - lastAiQuery >= 15000) {
            printf("AI_QUERY: [Sim] Node 34 reports high temperature. Recommend action.\n");
            fflush(stdout);
            lastAiQuery = now;
        }
        
        lastTick = now;
    }
}

int main(int argc, char** argv) {
    setup_sim();
    
    // Simple non-blocking loop (or could use a thread)
    bool running = true;
    while (running) {
        loop_sim();
        
        // Check for quit command on stdin
        // (This is just a placeholder; the Python bridge would use a more robust pipe)
        #ifdef _WIN32
        // Windows specific non-blocking stdin check could go here
        #endif
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
#endif
