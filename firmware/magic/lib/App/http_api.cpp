#include "../HAL/board_config.h"

#ifdef ENABLE_HTTP_API

#include <Arduino.h>
#include <string>
#include <vector>
#include "http_api.h"
#include "nvs_manager.h"
#include "status_builder.h"
#include "command_mx_bridge.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include "product_manager.h"
#include "../HAL/probe_manager.h"

// ============================================================================
// Static Member Initialization
// ============================================================================

AsyncWebServer* HttpAPI::server = nullptr;
AsyncWebSocket* HttpAPI::ws = nullptr;
bool HttpAPI::running = false;
uint16_t HttpAPI::port = 80;
std::string HttpAPI::cachedStatus = "";

// Forward declaration of internal help
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);

// ============================================================================
// Public Methods
// ============================================================================

bool HttpAPI::init() {
  Serial.printf("[HttpAPI] Initializing HTTP API server on port %u...\n", port);

  if (server != nullptr) {
    Serial.println("[HttpAPI] Server already initialized");
    return false;
  }

  // Create AsyncWebServer and WebSocket instances
  server = new AsyncWebServer(port);
  ws = new AsyncWebSocket("/ws");

  if (server == nullptr || ws == nullptr) {
    Serial.println("[HttpAPI] ERROR: Failed to allocate Server/WS");
    return false;
  }

  // Register WebSocket event handler
  ws->onEvent(onWsEvent);
  server->addHandler(ws);

  // Register route handlers
  server->on("/", HTTP_GET, handleRoot);
  server->on("/api/status", HTTP_GET, handleStatus);
  server->on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleBody);
  server->on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleBody);
  server->on("/api/ota", HTTP_GET, handleOTACheck);
  server->on("/api/ota/update", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, _handleOTAUpdateBody);

  // Provisioning & Config (Phase 3)
  server->on("/api/version", HTTP_GET, handleVersion);
  server->on("/api/config", HTTP_GET, handleConfig);
  server->on("/api/provision", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleBody);
  server->on("/api/reboot", HTTP_POST, handleReboot);

  // Product Management (V1 Parity)
  server->on("/api/products/list", HTTP_GET, handleListProducts);
  server->on("/api/products/load", HTTP_POST, handleLoadProduct);
  server->on("/api/products/save", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleBody);

  // Sniffer Control (Marauder Integration)
  server->on("/api/probe/sniff", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("enabled", true)) {
      bool enable = request->getParam("enabled", true)->value() == "true";
      ProbeManager::getInstance().setSniffing(enable);
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"missing enabled\"}");
    }
  });

  server->on("/api/probe/hop", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("enabled", true)) {
      bool enable = request->getParam("enabled", true)->value() == "true";
      ProbeManager::getInstance().setAutoHopping(enable);
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"missing enabled\"}");
    }
  });

  // Static files from LittleFS
  server->serveStatic("/", LittleFS, "/web/").setDefaultFile("index.html");

  // 404 handler (must be last)
  server->onNotFound(handle404);

  // Start server
  server->begin();
  running = true;

  Serial.printf("[HttpAPI] HTTP API server started on port %u\n", port);
  Serial.println("[HttpAPI] Routes: GET / GET /api/status POST /api/relay POST /api/command");

  return true;
}

void HttpAPI::shutdown() {
  if (server != nullptr) {
    server->end();
    if (ws != nullptr) {
      ws->closeAll();
      delete ws;
      ws = nullptr;
    }
    delete server;
    server = nullptr;
    running = false;
    Serial.println("[HttpAPI] Server shutdown");
  }
}

bool HttpAPI::isRunning() {
  return running;
}

void HttpAPI::updateStatus(const std::string& jsonStatus) {
  cachedStatus = jsonStatus;
  
  // Broadcast to all connected WebSocket clients for "near realtime" updates
  if (running && ws != nullptr && ws->count() > 0) {
    ws->textAll(jsonStatus.c_str());
  }
  Serial.printf("[HttpAPI] Status cache updated (%zu bytes)\n", jsonStatus.length());
}

std::string HttpAPI::getStatus() {
  return cachedStatus;
}

void HttpAPI::setPort(uint16_t newPort) {
  if (running) {
    Serial.println("[HttpAPI] Cannot change port while server is running");
    return;
  }
  port = newPort;
}

uint16_t HttpAPI::getPort() {
  return port;
}

// ============================================================================
// Request Handlers
// ============================================================================

void HttpAPI::handleRoot(AsyncWebServerRequest* request) {
  // Simple HTML page
  const char* html = R"html(
<!DOCTYPE html>
<html>
<head><title>Magic API</title></head>
<body>
<h1>Magic Device API</h1>
<p><a href="/api/status">View Device Status</a></p>
<p>Available endpoints:</p>
<ul>
<li>GET /api/status - Device status</li>
<li>POST /api/relay - Control relay</li>
<li>POST /api/command - Send command</li>
</ul>
</body>
</html>
  )html";

  AsyncWebServerResponse* response = request->beginResponse(200, "text/html", html);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handleStatus(AsyncWebServerRequest* request) {
  // Build full status JSON from all managers
  StaticJsonDocument<2048> statusDoc = StatusBuilder::buildStatus();

  String jsonStr;
  serializeJson(statusDoc, jsonStr);

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", jsonStr);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);

  Serial.println("[HttpAPI] GET /api/status");
}

void HttpAPI::handleRelay(AsyncWebServerRequest* request) {
  // Check if request has JSON body
  if (request->contentType() != "application/json") {
    DynamicJsonDocument doc(256);
    doc["status"] = "ERROR";
    doc["message"] = "Content-Type must be application/json";

    String jsonStr;
    serializeJson(doc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  // Parse JSON body
  if (request->_tempObject == nullptr) {
      request->send(400, "application/json", "{\"error\":\"empty body\"}");
      return;
  }
  String* bodyPtr = (String*)request->_tempObject;
  String body = *bodyPtr;
  delete bodyPtr;
  request->_tempObject = nullptr;

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.printf("[HttpAPI] JSON parse error: %s\n", error.c_str());
    DynamicJsonDocument errDoc(256);
    errDoc["status"] = "ERROR";
    errDoc["message"] = "Invalid JSON";

    String jsonStr;
    serializeJson(errDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  // Extract parameters
  const char* action = doc["action"];
  // durationMs handled inside CommandManager via RELAY command args if needed

  if (!action || (strcmp(action, "ON") != 0 && strcmp(action, "OFF") != 0)) {
    DynamicJsonDocument errDoc(256);
    errDoc["status"] = "ERROR";
    errDoc["message"] = "action must be ON or OFF";

    String jsonStr;
    serializeJson(errDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  // Execute relay control through CommandManager to ensure JSON persistence and OLED updates
  char cmdBuf[32];
  snprintf(cmdBuf, sizeof(cmdBuf), "RELAY 1 %s", action);
  
  String responseJson;
  CommandMxBridge::process(String(cmdBuf), [&responseJson](const String& resp) {
    responseJson = resp;
  });

  // Log relay control command
  Serial.printf("[HttpAPI] Relay command executed: %s\n", cmdBuf);

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", responseJson);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handleCommand(AsyncWebServerRequest* request) {
  // Check content type
  if (request->contentType() != "application/json") {
    DynamicJsonDocument doc(256);
    doc["status"] = "ERROR";
    doc["message"] = "Content-Type must be application/json";

    String jsonStr;
    serializeJson(doc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  // Parse JSON
  if (request->_tempObject == nullptr) {
    request->send(400, "application/json", "{\"status\":\"ERROR\",\"message\":\"Empty body\"}");
    return;
  }
  String* bodyPtr = (String*)request->_tempObject;
  String body = *bodyPtr;
  delete bodyPtr;
  request->_tempObject = nullptr;

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.printf("[HttpAPI] JSON parse error: %s\n", error.c_str());
    DynamicJsonDocument errDoc(256);
    errDoc["status"] = "ERROR";
    errDoc["message"] = "Invalid JSON";

    String jsonStr;
    serializeJson(errDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  // Extract command
  const char* cmd = doc["cmd"];
  if (!cmd) {
    DynamicJsonDocument errDoc(256);
    errDoc["status"] = "ERROR";
    errDoc["message"] = "cmd field is required";

    String jsonStr;
    serializeJson(errDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    return;
  }

  Serial.printf("[HttpAPI] POST /api/command cmd=%s\n", cmd);

  // Build command string: "CMD arg1 arg2" format for CommandManager
  String cmdStr = String(cmd);
  if (doc.containsKey("args")) {
    cmdStr += " " + doc["args"].as<String>();
  } else if (doc.containsKey("params")) {
    // Flatten params as space-separated for simple commands
    for (JsonPair p : doc["params"].as<JsonObject>()) {
      cmdStr += " " + String(p.value().as<const char*>());
    }
  }

  // Route through CommandMxBridge — handles STATUS, RELAY, SETWIFI, BLINK, REBOOT, HELP
  String responseJson;
  CommandMxBridge::process(cmdStr, [&responseJson](const String& resp) {
    responseJson = resp;
  });

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", responseJson);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handleOTACheck(AsyncWebServerRequest* request) {
  Serial.println("[HttpAPI] GET /api/ota (not implemented)");

  DynamicJsonDocument doc(256);
  doc["status"] = "OK";
  doc["message"] = "OTA check not yet implemented";
  doc["available"] = false;

  String jsonStr;
  serializeJson(doc, jsonStr);

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", jsonStr);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handleOTAUpdate(AsyncWebServerRequest* request) {
  Serial.println("[HttpAPI] POST /api/ota/update (not implemented)");

  DynamicJsonDocument doc(256);
  doc["status"] = "ERROR";
  doc["message"] = "OTA update not yet implemented";

  String jsonStr;
  serializeJson(doc, jsonStr);

  AsyncWebServerResponse* response = request->beginResponse(501, "application/json", jsonStr);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handle404(AsyncWebServerRequest* request) {
  Serial.printf("[HttpAPI] 404: %s %s\n",
                request->methodToString(),
                request->url().c_str());

  DynamicJsonDocument doc(256);
  doc["status"] = "ERROR";
  doc["message"] = "Endpoint not found";
  doc["path"] = request->url();

  String jsonStr;
  serializeJson(doc, jsonStr);

  AsyncWebServerResponse* response = request->beginResponse(404, "application/json", jsonStr);
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void HttpAPI::handleVersion(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(256);
  doc["version"] = FIRMWARE_VERSION;
  doc["hardware"] = HW_VERSION;
  doc["board"] = HW_VERSION;
  std::string role = NVSManager::getMeshConfigStr("role");
  doc["role"] = role.empty() ? "node" : role;
  
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

void HttpAPI::handleConfig(AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(2048);
  
  // Features
  JsonObject feats = doc.createNestedObject("features");
  const char* featKeys[] = {"mqtt", "gps", "ble", "espnow", "sensor", "oled", "scheduler", "mcp"};
  for (const char* k : featKeys) feats[k] = NVSManager::isFeatureEnabled(k);

  // Hardware Pins/Buses
  JsonObject hw = doc.createNestedObject("hw");
  const char* hwKeys[] = {"i2c_sda", "i2c_scl", "mcp_addr", "carrier"};
  for (const char* k : hwKeys) {
    int32_t val = NVSManager::getHardwareConfigInt(k);
    if (val != -1) hw[k] = val;
    else {
      std::string s = NVSManager::getHardwareConfigStr(k);
      if (!s.empty()) hw[k] = s;
    }
  }

  // Mesh/Identity
  JsonObject mesh = doc.createNestedObject("mesh");
  const char* meshKeys[] = {"node_id", "wifi_ssid", "role", "fleet_id"};
  for (const char* k : meshKeys) {
    std::string s = NVSManager::getMeshConfigStr(k);
    if (!s.empty()) mesh[k] = s;
  }
  
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

void HttpAPI::handleProvision(AsyncWebServerRequest* request) {
  if (request->_tempObject == nullptr) {
    request->send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }

  String body = *((String*)request->_tempObject);
  delete (String*)request->_tempObject;
  request->_tempObject = nullptr;

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  // Process features
  if (doc.containsKey("features")) {
    JsonObject feats = doc["features"];
    for (JsonPair p : feats) {
      NVSManager::setFeatureEnabled(p.key().c_str(), p.value().as<bool>());
    }
  }

  // Process hardware
  if (doc.containsKey("hw")) {
    JsonObject hw = doc["hw"];
    for (JsonPair p : hw) {
      if (p.value().is<int>()) NVSManager::setHardwareConfigInt(p.key().c_str(), p.value().as<int>());
      else NVSManager::setHardwareConfigStr(p.key().c_str(), p.value().as<String>().c_str());
    }
  }

  // Process mesh
  if (doc.containsKey("mesh")) {
    JsonObject mesh = doc["mesh"];
    for (JsonPair p : mesh) {
      NVSManager::setMeshConfigStr(p.key().c_str(), p.value().as<String>().c_str());
    }
  }

  request->send(200, "application/json", "{\"status\":\"ok\",\"action\":\"reboot_required\"}");
}

void HttpAPI::handleReboot(AsyncWebServerRequest* request) {
  request->send(200, "application/json", "{\"status\":\"rebooting\"}");
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP.restart();
}

void HttpAPI::addCORSHeaders(AsyncWebServerRequest* request) {
  // CORS is handled at the response level in each handler
}

// ============================================================================
// Product Handlers
// ============================================================================

void HttpAPI::handleListProducts(AsyncWebServerRequest* request) {
    String json = ProductManager::getInstance().listProducts();
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void HttpAPI::handleLoadProduct(AsyncWebServerRequest* request) {
    if (!request->hasParam("name", true)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing name parameter\"}");
        return;
    }
    String name = request->getParam("name", true)->value();
    if (ProductManager::getInstance().loadProduct(name)) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"Load failed\"}");
    }
}

void HttpAPI::handleSaveProduct(AsyncWebServerRequest* request) {
    String json;
    if (request->_tempObject) {
        String* bodyPtr = (String*)request->_tempObject;
        json = *bodyPtr;
        delete bodyPtr;
        request->_tempObject = nullptr;
    } else if (request->hasParam("json", true)) {
        json = request->getParam("json", true)->value();
    } else {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing json\"}");
        return;
    }
    
    // Extract name from JSON
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
    }
    
    String name = doc["name"] | "default";
    if (ProductManager::getInstance().saveProduct(name, json)) {
        request->send(200, "application/json", "{\"ok\":true}");
    } else {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}");
    }
}

// ============================================================================
// Internal WebSocket Event Handler
// ============================================================================

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[HttpAPI] WS Client connected from %s\n", client->remoteIP().toString().c_str());
    // Send current status immediately upon connection
    std::string currentStatus = HttpAPI::getStatus();
    if (!currentStatus.empty()) {
      client->text(currentStatus.c_str());
    }
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[HttpAPI] WS Client disconnected\n");
  }
}

void HttpAPI::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    request->_tempObject = new String();
  }
  if (request->_tempObject) {
    ((String*)request->_tempObject)->concat((char*)data, len);
  }
}

void HttpAPI::_handleOTAUpdateBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // OTA update body handling (future implementation)
}
#endif
