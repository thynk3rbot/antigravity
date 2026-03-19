/**
 * @file http_api.cpp
 * @brief HTTP API server implementation
 */

#include "http_api.h"
#include "nvs_manager.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ============================================================================
// Static Member Initialization
// ============================================================================

AsyncWebServer* HttpAPI::server = nullptr;
bool HttpAPI::running = false;
uint16_t HttpAPI::port = 80;
std::string HttpAPI::cachedStatus = "";

// ============================================================================
// Public Methods
// ============================================================================

bool HttpAPI::init() {
  Serial.printf("[HttpAPI] Initializing HTTP API server on port %u...\n", port);

  if (server != nullptr) {
    Serial.println("[HttpAPI] Server already initialized");
    return false;
  }

  // Create AsyncWebServer instance
  server = new AsyncWebServer(port);

  if (server == nullptr) {
    Serial.println("[HttpAPI] ERROR: Failed to allocate AsyncWebServer");
    return false;
  }

  // Register route handlers
  server->on("/", HTTP_GET, handleRoot);
  server->on("/api/status", HTTP_GET, handleStatus);
  server->on("/api/relay", HTTP_POST, handleRelay);
  server->on("/api/command", HTTP_POST, handleCommand);
  server->on("/api/ota", HTTP_GET, handleOTACheck);
  server->on("/api/ota/update", HTTP_POST, handleOTAUpdate);

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
<head><title>LoRaLink API</title></head>
<body>
<h1>LoRaLink Device API</h1>
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
  if (cachedStatus.empty()) {
    // Return placeholder if no status cached yet
    DynamicJsonDocument doc(512);
    doc["status"] = "unknown";
    doc["message"] = "Device status not yet cached";
    doc["uptime"] = millis() / 1000;

    String jsonStr;
    serializeJson(doc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  } else {
    // Return cached status
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", cachedStatus.c_str());
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  }

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
  String body = request->arg("plain");
  DynamicJsonDocument doc(256);
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
  uint32_t durationMs = doc["duration_ms"] | 0;

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

  // Log relay control command
  Serial.printf("[HttpAPI] POST /api/relay action=%s duration=%u ms\n",
                action, (unsigned int)durationMs);

  // TODO: Execute relay control through RelayHAL or command manager
  // For now, just acknowledge the request

  DynamicJsonDocument respDoc(256);
  respDoc["status"] = "OK";
  respDoc["relay"] = action;
  if (durationMs > 0) {
    respDoc["duration_ms"] = durationMs;
  }

  String jsonStr;
  serializeJson(respDoc, jsonStr);

  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", jsonStr);
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
  String body = request->arg("plain");
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

  // Handle specific commands
  if (strcmp(cmd, "SETWIFI") == 0) {
    const char* ssid = doc["params"]["ssid"];
    const char* pass = doc["params"]["pass"];

    if (!ssid || !pass) {
      DynamicJsonDocument errDoc(256);
      errDoc["status"] = "ERROR";
      errDoc["message"] = "SETWIFI requires ssid and pass parameters";

      String jsonStr;
      serializeJson(errDoc, jsonStr);

      AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
      return;
    }

    // Store WiFi credentials in NVS
    NVSManager::setWiFiSSID(ssid);
    NVSManager::setWiFiPassword(pass);

    Serial.printf("[HttpAPI] WiFi credentials updated: SSID=%s\n", ssid);

    DynamicJsonDocument respDoc(256);
    respDoc["status"] = "OK";
    respDoc["message"] = "WiFi credentials saved";
    respDoc["ssid"] = ssid;

    String jsonStr;
    serializeJson(respDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  } else {
    // Unknown command
    DynamicJsonDocument errDoc(256);
    errDoc["status"] = "ERROR";
    errDoc["message"] = "Unknown command";

    String jsonStr;
    serializeJson(errDoc, jsonStr);

    AsyncWebServerResponse* response = request->beginResponse(400, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
  }
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

void HttpAPI::addCORSHeaders(AsyncWebServerRequest* request) {
  // Add CORS headers to response
  // Note: AsyncWebServer headers are added via response object, not request
  // CORS can be added via response->addHeader() if needed
  // For now, we handle it at the response level in each handler
}
