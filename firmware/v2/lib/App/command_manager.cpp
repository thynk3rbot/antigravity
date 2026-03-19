#include "command_manager.h"
#include "nvs_config.h"
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
    help += "  STATUS              - Return JSON status blob\n";
    help += "  RELAY <1|2> <ON|OFF>- Control relay 1 or 2\n";
    help += "  SETWIFI <SSID> <PW> - Set WiFi credentials (PW may contain spaces)\n";
    help += "  BLINK               - Blink LED 3 times\n";
    help += "  REBOOT              - Reboot the device\n";
    help += "  HELP                - Show this help message\n";
    help += "  GETCONFIG           - Return current NVS configuration as JSON";
    return help;
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
