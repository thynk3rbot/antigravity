#include "command_manager.h"
#include "nvs_config.h"
#include "schedule_manager.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
CommandManager::RelayCallback CommandManager::_relayCallback = nullptr;
CommandManager::StatusData    CommandManager::_lastStatus    = {};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CommandManager::begin() {
    // Nothing to initialise for now — NVSConfig::begin() is called by main.
}

void CommandManager::setRelayCallback(RelayCallback cb) {
    _relayCallback = cb;
}

void CommandManager::updateStatus(const StatusData& data) {
    _lastStatus = data;
}

void CommandManager::process(const String& input, ResponseCallback responseCallback) {
    String trimmed = input;
    trimmed.trim();
    if (trimmed.length() == 0) return;

    String cmd, args;
    _parseCommand(trimmed, cmd, args);

    String response;
    if (cmd == "STATUS") {
        response = _handleStatus();
    } else if (cmd == "RELAY") {
        response = _handleRelay(args);
    } else if (cmd == "SETWIFI") {
        response = _handleSetWifi(args);
    } else if (cmd == "BLINK") {
        response = _handleBlink();
    } else if (cmd == "REBOOT") {
        response = _handleReboot();
    } else if (cmd == "HELP") {
        response = _handleHelp();
    } else if (cmd == "GETCONFIG") {
        response = _handleGetConfig();
    } else if (cmd == "SCHED") {
        response = _handleSched(args);
    } else {
        response = "{\"ok\":false,\"error\":\"Unknown command: " + cmd + "\"}";
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
    String json = "{";
    json += "\"node_id\":\""    + s.nodeId      + "\",";
    json += "\"version\":\""    + s.version     + "\",";
    json += "\"ip\":\""         + s.ipAddr      + "\",";
    json += "\"bat_v\":"        + String(s.batVoltage, 2) + ",";
    json += "\"bat_pct\":"      + String(s.batPercent)    + ",";
    json += "\"power_mode\":\"" + s.powerMode   + "\",";
    json += "\"relay1\":"       + String(s.relay1  ? "true" : "false") + ",";
    json += "\"relay2\":"       + String(s.relay2  ? "true" : "false") + ",";
    json += "\"lora_rssi\":"    + String(s.loraRSSI) + ",";
    json += "\"lora_snr\":"     + String(s.loraSNR, 1) + ",";
    json += "\"lora_tx\":"      + String(s.loraTX)   + ",";
    json += "\"lora_rx\":"      + String(s.loraRX)   + ",";
    json += "\"mesh_neighbors\":" + String(s.meshNeighbors) + ",";
    json += "\"uptime\":"       + String(s.uptime)   + ",";
    json += "\"free_heap\":"    + String(s.freeHeap);
    json += "}";
    return json;
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
    if (relayNum < 1 || relayNum > 2) {
        return "{\"ok\":false,\"error\":\"Relay number must be 1 or 2\"}";
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
    NVSConfig::setRelayState((uint8_t)relayNum, state);

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

    bool ok = NVSConfig::setWifiCredentials(ssid, pass);
    if (ok) {
        return "{\"ok\":true}";
    } else {
        return "{\"ok\":false,\"error\":\"Failed to save WiFi credentials\"}";
    }
}

String CommandManager::_handleBlink() {
#ifdef LED_BUILTIN
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(200);
        digitalWrite(LED_BUILTIN, LOW);
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
    help += "  STATUS                          - Return JSON status blob\n";
    help += "  RELAY <1|2> <ON|OFF>            - Control relay 1 or 2\n";
    help += "  SETWIFI <SSID> <PW>             - Set WiFi credentials\n";
    help += "  BLINK                           - Blink LED 3 times\n";
    help += "  REBOOT                          - Reboot the device\n";
    help += "  GETCONFIG                       - Return NVS config as JSON\n";
    help += "  SCHED LIST                      - List all scheduled tasks\n";
    help += "  SCHED ADD <n> <type> <pin> <s>  - Add task (type: TOGGLE PULSE ON OFF PWM READ)\n";
    help += "  SCHED REM <name>                - Remove task by name\n";
    help += "  SCHED ENABLE/DISABLE <name>     - Enable or pause a task\n";
    help += "  SCHED CLEAR                     - Remove all tasks\n";
    help += "  SCHED SAVE                      - Persist tasks to flash\n";
    help += "  HELP                            - Show this help message";
    return help;
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
    json += "\"node_id\":\""   + NVSConfig::getNodeId()      + "\",";
    json += "\"wifi_ssid\":\"" + NVSConfig::getWifiSSID()    + "\",";
    json += "\"power_mode\":"  + String(NVSConfig::getPowerMode()) + ",";
    json += "\"relay1\":"      + String(NVSConfig::getRelayState(1) ? "true" : "false") + ",";
    json += "\"relay2\":"      + String(NVSConfig::getRelayState(2) ? "true" : "false");
    json += "}";
    return json;
}
