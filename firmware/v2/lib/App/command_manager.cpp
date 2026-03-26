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
#include "status_builder.h"
#include "power_manager.h"
#include "../Transport/lora_transport.h"
#include "../Transport/wifi_transport.h"
#include "mesh_coordinator.h"
#include <vector>

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
        DynamicJsonDocument doc(512);
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
    else if (trimmed.indexOf('=') > 0 && !trimmed.startsWith("RELAY")) { 
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
        args.replace(',', ' '); 
    }
    // 4. Fallback to Plaintext
    else {
        _parseCommand(trimmed, cmd, args);
    }

    cmd.toUpperCase(); 
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

String CommandManager::_handleStatus() {
    DynamicJsonDocument doc(1024);
    JsonObject root = doc.to<JsonObject>();
    
    root["name"] = String(NVSManager::getNodeID("Node").c_str());
    root["ver"] = FIRMWARE_VERSION;
    root["ip"] = WiFi.localIP().toString();
    root["bat_pct"] = (int)PowerManager::getBatteryPercent();
    
    PowerMode mode = PowerManager::getMode();
    root["mode"] = (mode == PowerMode::CONSERVE) ? "CONSERVE" : 
                   (mode == PowerMode::CRITICAL) ? "CRITICAL" : "NORMAL";
    
    root["vext"] = (bool)PowerManager::isVEXTStable();
    root["lora_rssi"] = (int)LoRaTransport::getInstance().getSignalStrength();
    root["peer_cnt"] = (int)MeshCoordinator::instance().getNeighborCount();
    root["uptime"] = (uint32_t)(millis() / 1000);

#ifdef HAS_GPS
    GPSManager::GPSData gps = GPSManager::getData();
    JsonObject gpsObj = root.createNestedObject("gps");
    gpsObj["lat"] = gps.lat;
    gpsObj["lon"] = gps.lon;
    gpsObj["alt"] = gps.alt;
#endif

    String response;
    serializeJson(doc, response);
    return response;
}

String CommandManager::_handleVStatus() {
    return String(StatusBuilder::buildStatusString().c_str());
}

String CommandManager::_handleRelay(const String& args) {
    String a = args;
    a.trim();
    int spaceIdx = a.indexOf(' ');
    if (spaceIdx < 0) return "{\"ok\":false,\"error\":\"Usage: RELAY <1|2> <ON|OFF>\"}";
    String relayStr = a.substring(0, spaceIdx);
    String stateStr = a.substring(spaceIdx + 1);
    stateStr.trim(); stateStr.toUpperCase();
    int relayNum = relayStr.toInt();
    if (relayNum < 1 || relayNum > 8) return "{\"ok\":false,\"error\":\"Relay number must be 1 to 8\"}";
    bool state = (stateStr == "ON" || stateStr == "1" || stateStr == "TRUE");
    if (_relayCallback) _relayCallback((uint8_t)relayNum, state);
    NVSManager::setRelayState((uint8_t)relayNum, state);
    return "{\"ok\":true,\"relay\":" + String(relayNum) + ",\"state\":" + (state ? "true" : "false") + "}";
}

String CommandManager::_handleSetWifi(const String& args) {
    String a = args; a.trim();
    int spaceIdx = a.indexOf(' ');
    if (spaceIdx < 0) return "{\"ok\":false,\"error\":\"Usage: SETWIFI <SSID> <PASSWORD>\"}";
    String ssid = a.substring(0, spaceIdx);
    String pass = a.substring(spaceIdx + 1);
    if (ssid.length() == 0) return "{\"ok\":false,\"error\":\"SSID cannot be empty\"}";
    if (NVSManager::setWiFiSSID(ssid.c_str()) && NVSManager::setWiFiPassword(pass.c_str())) return "{\"ok\":true}";
    return "{\"ok\":false,\"error\":\"Failed to save WiFi credentials\"}";
}

String CommandManager::_handleBlink() {
#if defined(PIN_LED) || defined(LED_BUILTIN)
    #ifndef PIN_LED
        #define PIN_LED LED_BUILTIN
    #endif
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH); delay(200);
        digitalWrite(PIN_LED, LOW); delay(200);
    }
#endif
    return "{\"ok\":true}";
}

String CommandManager::_handleReboot() {
    delay(1000);
    ESP.restart();
    return "{\"ok\":true}";
}

String CommandManager::_handleHelp() {
    return "Available commands: STATUS, VSTATUS, RELAY, SETNAME, SETWIFI, SETIP, BLINK, REBOOT, GETCONFIG, SCHED, FORWARD, GPS, ASK, LIST, LOAD, FACTORY_RESET, HELP";
}

String CommandManager::_handleSetName(const String& args) {
    String name = args; name.trim();
    if (name.length() == 0) return "{\"ok\":false,\"error\":\"Name cannot be empty\"}";
    if (NVSManager::setNodeID(name.c_str())) {
        delay(500); ESP.restart();
        return "{\"ok\":true,\"node_id\":\"" + name + "\",\"msg\":\"Rebooting...\"}";
    }
    return "{\"ok\":false,\"error\":\"NVS Save Failed\"}";
}

String CommandManager::_handleGetConfig() {
    return "{\"node_id\":\"" + String(NVSManager::getNodeID("Node").c_str()) + "\"}";
}

String CommandManager::_handleSetIP(const String& args) {
    return "{\"ok\":false,\"error\":\"Not implemented in this stub\"}";
}

String CommandManager::_handleSched(const String& args) {
    return "{\"ok\":false,\"error\":\"Not implemented in this stub\"}";
}

String CommandManager::_handleForward(const String& args) {
    return "{\"ok\":false,\"error\":\"Not implemented in this stub\"}";
}

String CommandManager::_handleGPS(const String& args) {
    return GPSManager::handleCommand(args);
}

String CommandManager::_handleAsk(const String& args) {
    Serial.print("AI_QUERY:"); Serial.println(args);
    return "{\"ok\":true}";
}

String CommandManager::_handleFactoryReset() {
    NVSManager::clearAll(); delay(1000); ESP.restart();
    return "{\"ok\":true}";
}

String CommandManager::_handleListProducts() {
    return ProductManager::getInstance().listProducts();
}

String CommandManager::_handleLoadProduct(const String& args) {
    if (ProductManager::getInstance().loadProduct(args)) return "{\"ok\":true}";
    return "{\"ok\":false}";
}

String CommandManager::_handleSetKey(const String& args) {
    return "{\"ok\":false}";
}

String CommandManager::_handleGPIO(const String& args) {
    return "{\"ok\":false}";
}

String CommandManager::_handleReadPin(const String& args) {
    return "{\"ok\":false}";
}

String CommandManager::_handleNodes() {
    return "{\"ok\":false}";
}

String CommandManager::_handleRepeater(const String& args) {
    return "{\"ok\":false}";
}

String CommandManager::_handleSleep(const String& args) {
    return "{\"ok\":false}";
}
