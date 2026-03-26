#include "command_manager.h"
#include "nvs_manager.h"
#include "schedule_manager.h"
#include "gps_manager.h"
#include <Arduino.h>
#ifndef UNIT_TEST
#include <WiFi.h>
#endif
#include "../Transport/message_router.h"
#include "control_packet.h"
#include "product_manager.h"
#include <ArduinoJson.h>
#include "plugin_manager.h"
#include "../HAL/mcp_manager.h"

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
CommandManager::RelayCallback CommandManager::_relayCallback = nullptr;
CommandManager::StatusData    CommandManager::_lastStatus    = {};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CommandManager::begin() {
#if defined(PIN_LED) || defined(LED_BUILTIN)
    #ifndef PIN_LED
        #define PIN_LED LED_BUILTIN
    #endif
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
#endif
}

void CommandManager::setRelayCallback(CommandManager::RelayCallback cb) {
    _relayCallback = cb;
}

void CommandManager::updateStatus(const StatusData& data) {
    _lastStatus = data;
}

void CommandManager::process(const String& input, CommandManager::ResponseCallback responseCallback) {
    String trimmed = input;
    trimmed.trim();
    if (trimmed.length() == 0) return;

    String cmd, args;
    
    // 1. Try JSON Decoding
    if (trimmed.startsWith("{")) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, trimmed);
        if (!error) {
            cmd = doc["cmd"].as<String>();
            args = doc["args"].as<String>();
            if (args == "null") args = ""; 
        } else {
            _parseCommand(trimmed, cmd, args);
        }
    } 
    // 2. Try KV Decoding (key=val)
    else if (trimmed.indexOf('=') > 0 && !trimmed.startsWith("RELAY")) { // Avoid mapping RELAY command args
        // Very basic KV: CMD=name ARGS=rest
        if (trimmed.indexOf("CMD=") >= 0) {
            int cmdStart = trimmed.indexOf("CMD=") + 4;
            int cmdEnd = trimmed.indexOf(' ', cmdStart);
            if (cmdEnd < 0) cmdEnd = trimmed.length();
            cmd = trimmed.substring(cmdStart, cmdEnd);
            
            if (trimmed.indexOf("ARGS=") >= 0) {
                args = trimmed.substring(trimmed.indexOf("ARGS=") + 5);
            }
        } else {
            _parseCommand(trimmed, cmd, args);
        }
    }
    // 3. Try CSV Decoding
    else if (trimmed.indexOf(',') > 0) {
        int commaIdx = trimmed.indexOf(',');
        cmd = trimmed.substring(0, commaIdx);
        args = trimmed.substring(commaIdx + 1);
        args.replace(',', ' '); // Convert remaining commas to spaces for internal parsing
    }
    // 4. Fallback to Plaintext
    else {
        _parseCommand(trimmed, cmd, args);
    }

    cmd.toUpperCase(); // Ensure consistency for all parsing paths
    String response;
    if (cmd == "STATUS") {
        response = _handleStatus();
    } else if (cmd == "VSTATUS") {
        response = _handleVStatus();
    } else if (cmd == "RELAY") {
        response = _handleRelay(args);
    } else if (cmd == "SETWIFI") {
        response = _handleSetWifi(args);
    } else if (cmd == "SETIP") {
        response = _handleSetIP(args);
    } else if (cmd == "BLINK") {
        response = _handleBlink();
    } else if (cmd == "REBOOT") {
        response = _handleReboot();
    } else if (cmd == "HELP") {
        response = _handleHelp();
    } else if (cmd == "GETCONFIG") {
        response = _handleGetConfig();
    } else if (cmd == "SETNAME") {
        response = _handleSetName(args);
    } else if (cmd == "SCHED") {
        response = _handleSched(args);
    } else if (cmd == "FORWARD") {
        response = _handleForward(args);
    } else if (cmd == "GPS") {
        response = _handleGPS(args);
    } else if (cmd == "ASK") {
        response = _handleAsk(args);
    } else if (cmd == "FACTORY_RESET") {
        response = _handleFactoryReset();
    } else if (cmd == "LIST") {
        response = _handleListProducts();
    } else if (cmd == "LOAD") {
        response = _handleLoadProduct(args);
    } else if (cmd == "SETKEY") {
        response = _handleSetKey(args);
    } else if (cmd == "GPIO") {
        response = _handleGPIO(args);
    } else if (cmd == "READ") {
        response = _handleReadPin(args);
    } else if (cmd == "NODES") {
        response = _handleNodes();
    } else if (cmd == "REPEATER") {
        response = _handleRepeater(args);
    } else if (cmd == "SLEEP") {
        response = _handleSleep(args);
    } else {
        // Delegate to plugins
        for (auto* plugin : PluginManager::getInstance().getPlugins()) {
            String pluginResponse = plugin->handleCommand(cmd, args);
            if (pluginResponse.length() > 0) {
                response = pluginResponse;
                break;
            }
        }
        
        if (response.length() == 0) {
            response = "{\"ok\":false,\"error\":\"Unknown command: " + cmd + "\"}";
        }
    }

    if (responseCallback) {
        responseCallback(response);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CommandManager::_parseCommand(const String& input, String& cmd, String& args) {
    int spaceIdx = input.indexOf(' ');
    if (spaceIdx < 0) {
        cmd  = input;
        args = "";
    } else {
        cmd  = input.substring(0, spaceIdx);
        args = input.substring(spaceIdx + 1);
    }
    cmd.toUpperCase();
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

String CommandManager::_handleStatus() {
    const StatusData& s = _lastStatus;
    StaticJsonDocument<512> doc;
    doc["name"] = s.nodeId;
    doc["id"] = s.meshId;
    doc["ver"] = s.version;
    doc["ip"] = s.ipAddr;
    doc["bat_v"] = serialized(String(s.batVoltage, 2));
    doc["bat_pct"] = s.batPercent;
    doc["mode"] = s.powerMode;
    doc["rssi"] = s.loraRSSI;
    doc["peer_cnt"] = s.meshNeighbors;
    doc["uptime"] = s.uptime;
    doc["relay_mask"] = (s.relay1 ? 0x01 : 0) | (s.relay2 ? 0x02 : 0);

    String response;
    serializeJson(doc, response);
    return response;
}

String CommandManager::_handleVStatus() {
    const StatusData& s = _lastStatus;
    StaticJsonDocument<1024> doc;
    doc["name"] = s.nodeId;
    doc["id"] = s.meshId;
    doc["ver"] = s.version;
    doc["hw"] = s.hw;
    doc["ip"] = s.ipAddr;
    doc["bat_v"] = serialized(String(s.batVoltage, 2));
    doc["bat_pct"] = s.batPercent;
    doc["mode"] = s.powerMode;
    doc["rssi"] = s.loraRSSI;
    doc["peer_cnt"] = s.meshNeighbors;
    doc["uptime"] = s.uptime;
    doc["heap"] = s.freeHeap / 1024;
    doc["relay_mask"] = (s.relay1 ? 0x01 : 0) | (s.relay2 ? 0x02 : 0);

    String response;
    serializeJson(doc, response);
    return response;
}

String CommandManager::_handleRelay(const String& args) {
    // Expected: "1 ON" / "1 OFF" / "2 ON" / "2 OFF"
    String a = args;
    a.trim();

    int spaceIdx = a.indexOf(' ');
    if (spaceIdx < 0) {
        return "{\"ok\":false,\"error\":\"Usage: RELAY <1|2> <ON|OFF>\"}";
    }

    String relayStr = a.substring(0, spaceIdx);
    String stateStr = a.substring(spaceIdx + 1);
    stateStr.trim();
    stateStr.toUpperCase();

    int relayNum = relayStr.toInt();
    if (relayNum < 1 || relayNum > 8) {
        return "{\"ok\":false,\"error\":\"Relay number must be 1 to 8\"}";
    }

    bool state;
    if (stateStr == "ON" || stateStr == "1" || stateStr == "TRUE") {
        state = true;
    } else if (stateStr == "OFF" || stateStr == "0" || stateStr == "FALSE") {
        state = false;
    } else {
        return "{\"ok\":false,\"error\":\"State must be ON or OFF\"}";
    }

    if (_relayCallback) {
        _relayCallback((uint8_t)relayNum, state);
    }

    // Persist state to NVS
    NVSManager::setRelayState((uint8_t)relayNum, state);

    String json = "{\"ok\":true,\"relay\":";
    json += String(relayNum);
    json += ",\"state\":";
    json += (state ? "true" : "false");
    json += "}";
    return json;
}

String CommandManager::_handleSetWifi(const String& args) {
    // Split on first space only — password may contain spaces
    String a = args;
    a.trim();

    int spaceIdx = a.indexOf(' ');
    if (spaceIdx < 0) {
        return "{\"ok\":false,\"error\":\"Usage: SETWIFI <SSID> <PASSWORD>\"}";
    }

    String ssid = a.substring(0, spaceIdx);
    String pass = a.substring(spaceIdx + 1);

    if (ssid.length() == 0) {
        return "{\"ok\":false,\"error\":\"SSID cannot be empty\"}";
    }

    bool ok = NVSManager::setWiFiSSID(ssid.c_str()) && NVSManager::setWiFiPassword(pass.c_str());
    if (ok) {
        return "{\"ok\":true}";
    } else {
        return "{\"ok\":false,\"error\":\"Failed to save WiFi credentials\"}";
    }
}

String CommandManager::_handleBlink() {
#if defined(PIN_LED) || defined(LED_BUILTIN)
    #ifndef PIN_LED
        #define PIN_LED LED_BUILTIN
    #endif
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(200);
        digitalWrite(PIN_LED, LOW);
        delay(200);
    }
#endif
    return "{\"ok\":true}";
}

String CommandManager::_handleReboot() {
    // Return response first, then reboot after 1 s
    delay(1000);
    ESP.restart();
    return "{\"ok\":true}";  // Never reached, but satisfies return type
}

String CommandManager::_handleHelp() {
    String help = "Available commands:\n";
    help += "  STATUS                          - Friendly JSON status\n";
    help += "  VSTATUS                         - Verbose technical JSON status\n";
    help += "  RELAY <1-8> <ON|OFF>            - Control relay channel (1-8)\n";
    help += "  SETNAME <name>                  - Set node ID\n";
    help += "  SETWIFI <SSID> <PW>             - Set WiFi credentials\n";
    help += "  SETIP <IP> [GW] [SN]            - Set static IP (Reboot required)\n";
    help += "  BLINK                           - Blink LED 3 times\n";
    help += "  REBOOT                          - Reboot the device\n";
    help += "  GETCONFIG                       - Return NVS config as JSON\n";
    help += "  SCHED ENABLE/DISABLE <name>     - Enable or pause a task\n";
    help += "  SCHED CLEAR                     - Remove all tasks\n";
    help += "  SCHED SAVE                      - Persist tasks to flash\n";
    help += "  FORWARD <id> <cmd>              - Forward command to mesh node\n";
    help += "  GPS [ON|OFF]                    - Power or status of GNSS\n";
    help += "  ASK <prompt>                    - Send query to Local AI Workstation\n";
    help += "  LIST                            - List stored Peripheral Products (.json)\n";
    help += "  LOAD <name>                     - Load a specific Product profile\n";
    help += "  FACTORY_RESET                   - Clear all settings and reboot\n";
    help += "  HELP                            - Show this help message";
    return help;
}

String CommandManager::_handleAsk(const String& args) {
    if (args.length() == 0) {
        return "{\"ok\":false,\"error\":\"Usage: ASK <prompt>\"}";
    }

    // Route the query to the PC Daemon via Serial streamer
    // This allows the RAG Router or other daemons to intercept the query.
    Serial.print("AI_QUERY:");
    Serial.println(args);

    return "{\"ok\":true,\"msg\":\"Query sent to AI Workstation\"}";
}

String CommandManager::_handleForward(const String& args) {
    // ... Existing implementation ...
    return ""; // Placeholder for brevities, tool will replace target exactly
}

String CommandManager::_handleGPS(const String& args) {
    return GPSManager::handleCommand(args);
}

String CommandManager::_handleSetName(const String& args) {
    String name = args;
    name.trim();
    if (name.length() == 0) {
        return "{\"ok\":false,\"error\":\"Name cannot be empty\"}";
    }
    bool ok = NVSManager::setNodeID(name.c_str());
    if (ok) {
        Serial.printf("[CMD] Node name set to: %s. Rebooting...\n", name.c_str());
        delay(500);
        ESP.restart();
        return "{\"ok\":true,\"node_id\":\"" + name + "\",\"msg\":\"Rebooting...\"}";
    }
    return "{\"ok\":false,\"error\":\"NVS Save Failed\"}";
}

String CommandManager::_handleFactoryReset() {
    Serial.println("[CMD] Factory Reset initiated. Clearing NVS and Schedules...");
    ScheduleManager::clearTasks();
    NVSManager::clearAll();
    delay(1000);
    ESP.restart();
    return "{\"ok\":true,\"msg\":\"Factory reset initialized. Rebooting...\"}";
}

String CommandManager::_handleSetIP(const String& args) {
    // Format: SETIP <IP> <GW> <SN>
    String a = args;
    a.trim();
    // Split args
    String ip, gw, sn;
    int space1 = a.indexOf(' ');
    if (space1 < 0) {
        if (a.length() == 0 || a == "CLEAR" || a == "DHCP") {
            NVSManager::setStaticIP("");
            return "{\"ok\":true,\"msg\":\"Static IP cleared. DHCP will be used after reboot.\"}";
        }
        // Only one arg: just the IP
        ip = a;
        gw = "";
        sn = "";
    } else {
        ip = a.substring(0, space1);
        String rest = a.substring(space1 + 1);
        rest.trim();

        int space2 = rest.indexOf(' ');
        if (space2 < 0) {
            gw = rest;
            sn = "";
        } else {
            gw = rest.substring(0, space2);
            sn = rest.substring(space2 + 1);
            sn.trim();
        }
    }

    // Basic validation
    IPAddress test;
    if (!test.fromString(ip)) {
        return "{\"ok\":false,\"error\":\"Invalid IP address format\"}";
    }

    if (gw.length() > 0 && !test.fromString(gw)) {
        return "{\"ok\":false,\"error\":\"Invalid Gateway address format\"}";
    }

    if (sn.length() > 0 && !test.fromString(sn)) {
        return "{\"ok\":false,\"error\":\"Invalid Subnet mask format\"}";
    }

    // Default gateway/subnet if not provided
    if (gw.length() == 0) gw = "0.0.0.0";
    if (sn.length() == 0) sn = "255.255.255.0";

    bool ok = NVSManager::setStaticIP(ip.c_str()) && 
              NVSManager::setGateway(gw.c_str()) && 
              NVSManager::setSubnet(sn.c_str());

    if (ok) {
        return "{\"ok\":true,\"msg\":\"Static IP set. Reboot required.\"}";
    }
    return "{\"ok\":false,\"error\":\"NVS Save Failed\"}";
}

String CommandManager::_handleSched(const String& args) {
    String a = args;
    a.trim();

    // Extract sub-command
    int spaceIdx = a.indexOf(' ');
    String sub  = (spaceIdx < 0) ? a : a.substring(0, spaceIdx);
    String rest = (spaceIdx < 0) ? "" : a.substring(spaceIdx + 1);
    sub.toUpperCase();

    if (sub == "LIST") {
        return ScheduleManager::getReport();

    } else if (sub == "SAVE") {
        ScheduleManager::saveSchedules();
        return "{\"ok\":true,\"msg\":\"Schedules saved\"}";

    } else if (sub == "CLEAR") {
        ScheduleManager::clearTasks();
        return "{\"ok\":true,\"msg\":\"All schedules cleared\"}";

    } else if (sub == "ADD") {
        // Format: ADD <name> <type> <pin> <interval_s> [duration_s]
        // e.g.  : ADD PumpA TOGGLE 32 30
        //         ADD ValveB PULSE 33 600 5
        String parts[6];
        int count = 0;
        int start = 0;
        for (int i = 0; i <= (int)rest.length() && count < 6; i++) {
            if (i == (int)rest.length() || rest[i] == ' ') {
                if (i > start) parts[count++] = rest.substring(start, i);
                start = i + 1;
            }
        }
        if (count < 4) {
            return "{\"ok\":false,\"error\":\"Usage: SCHED ADD <name> <type> <pin> <interval_s> [duration_s]\"}";
        }
        String name     = parts[0];
        String type     = parts[1]; type.toUpperCase();
        int    pin      = parts[2].toInt();
        unsigned long iv  = parts[3].toInt();
        unsigned long dur = (count >= 5) ? parts[4].toInt() : 0;

        bool ok = ScheduleManager::addTask(name, type, pin, iv, dur, "CMD");
        if (ok) {
            return "{\"ok\":true,\"name\":\"" + name + "\",\"type\":\"" + type +
                   "\",\"pin\":" + String(pin) + ",\"interval_s\":" + String(iv) + "}";
        }
        return "{\"ok\":false,\"error\":\"Failed to add task (low heap or bad args)\"}";

    } else if (sub == "REM") {
        String name = rest;
        name.trim();
        bool ok = ScheduleManager::removeTask(name);
        if (ok) return "{\"ok\":true,\"removed\":\"" + name + "\"}";
        return "{\"ok\":false,\"error\":\"Task not found: " + name + "\"}";

    } else if (sub == "ENABLE") {
        String name = rest; name.trim();
        bool ok = ScheduleManager::enableTask(name);
        return ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Task not found\"}";

    } else if (sub == "DISABLE") {
        String name = rest; name.trim();
        bool ok = ScheduleManager::disableTask(name);
        return ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Task not found\"}";

    } else {
        return "{\"ok\":false,\"error\":\"Unknown SCHED sub-command. Try: LIST ADD REM CLEAR SAVE ENABLE DISABLE\"}";
    }
}

String CommandManager::_handleGetConfig() {
    String json = "{";
    json += "\"node_id\":\""   + String(NVSManager::getNodeID("Node").c_str())      + "\",";
    json += "\"active_prod\":\"" + ProductManager::getInstance().getActiveProduct() + "\",";
    json += "\"wifi_ssid\":\"" + String(NVSManager::getWiFiSSID().c_str())    + "\",";
    json += "\"power_mode\":"  + String(NVSManager::getPowerMode(0)) + ",";
    json += "\"relay1\":"      + String(NVSManager::getRelayState(1) ? "true" : "false") + ",";
    json += "\"relay2\":"      + String(NVSManager::getRelayState(2) ? "true" : "false");
    json += "}";
    return json;
}

String CommandManager::_handleListProducts() {
    return ProductManager::getInstance().listProducts();
}

String CommandManager::_handleLoadProduct(const String& args) {
    String name = args;
    name.trim();
    if (name.length() == 0) return "{\"ok\":false,\"error\":\"Usage: LOAD <product_name>\"}";
    
    if (ProductManager::getInstance().loadProduct(name)) {
        return "{\"ok\":true,\"msg\":\"Loaded product: " + name + "\"}";
    }
    return "{\"ok\":false,\"error\":\"Failed to load product: " + name + "\"}";
}
// Project-specific handlers removed (NutriCalc decoupled)

String CommandManager::_handleSetKey(const String& args) {
    String key = args;
    key.trim();
    if (key.length() != 32) return "{\"ok\":false,\"error\":\"Key must be 32 hex chars\"}";
    
    uint8_t rawKey[16];
    if (Crypto::hexToBytes(key, rawKey, 16)) {
        if (NVSManager::setCryptoKey(rawKey)) {
            return "{\"ok\":true,\"msg\":\"AES Key Updated. Reboot required.\"}";
        }
    }
    return "{\"ok\":false,\"error\":\"Save failed\"}";
}

String CommandManager::_handleGPIO(const String& args) {
    int space = args.indexOf(' ');
    if (space <= 0) return "{\"ok\":false,\"error\":\"Usage: GPIO <pin> <0|1>\"}";
    
    int pin = args.substring(0, space).toInt();
    int val = args.substring(space+1).toInt();
    
    if (MCPManager::isMcpPin(pin)) {
        MCPManager::setupPin(pin, OUTPUT);
        MCPManager::writePin(pin, val == 1);
    } else {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val == 1);
    }
    return "{\"ok\":true,\"pin\":" + String(pin) + ",\"val\":" + String(val) + "}";
}

String CommandManager::_handleReadPin(const String& args) {
    int pin = args.toInt();
    bool val = false;
    if (MCPManager::isMcpPin(pin)) {
        val = MCPManager::readPin(pin);
    } else {
        pinMode(pin, INPUT);
        val = digitalRead(pin);
    }
    return "{\"ok\":true,\"pin\":" + String(pin) + ",\"val\":" + String(val ? 1 : 0) + "}";
}

String CommandManager::_handleNodes() {
    // This requires access to the Mesh registry, which is currently in DataManager (Legacy)
    // or MessageRouter (V2). For now, return a placeholder until V2 Mesh registry is finalized.
    return "{\"ok\":true,\"msg\":\"Mesh node list not yet ported to V2 registry\"}";
}

String CommandManager::_handleRepeater(const String& args) {
    String a = args; a.trim(); a.toUpperCase();
    bool enable = (a == "ON" || a == "1");
    // NVSManager::setRepeater(enable); // Needs implementation in NVSConfig
    return "{\"ok\":true,\"repeater\":" + String(enable ? "true" : "false") + "}";
}

String CommandManager::_handleSleep(const String& args) {
    float hours = args.toFloat();
    if (hours <= 0) hours = 1.0f; // Default 1 hour

    // Guards from legacy
    float bat = _lastStatus.batVoltage;
    bool isPowered = (bat < 0.1f || bat > 4.25f);
    
    if (isPowered) {
        return "{\"ok\":false,\"error\":\"Sleep blocked: USB/Mains power detected\"}";
    }

    // Check if PC attached (based on last status/activity - simplified for now)
    // In v2, we can check if the last status update was recent or if the webapp is discoverying
    
    Serial.printf("[CMD] Deep sleep for %.2f hours initiated...\n", hours);
    
    // Convert hours to microseconds
    uint64_t sleepUs = (uint64_t)(hours * 3600.0f * 1000000.0f);
    esp_sleep_enable_timer_wakeup(sleepUs);
    
    // Give time for response to be sent
    vTaskDelay(pdMS_TO_TICKS(500));
#ifndef UNIT_TEST
    esp_deep_sleep_start();
#endif
    
    return "{\"ok\":true,\"msg\":\"Entering deep sleep\"}";
}
