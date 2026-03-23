#include "product_manager.h"
#include "nvs_config.h"
using namespace ArduinoJson;
#include "schedule_manager.h"
#include "../HAL/mcp_manager.h"
#include "../Transport/message_router.h"
#include "../Transport/mqtt_transport.h"
#include "control_packet.h"
#include <LittleFS.h>

bool ProductManager::init() {
    _ensureDir();
    restoreActiveProduct();
    broadcastRegistry(); // Announce all available tools
    return true;
}

void ProductManager::broadcastRegistry() {
    String list = listProducts();
    #ifdef ENABLE_MQTT_TRANSPORT
      if (MQTTTransport::instance()->isConnected()) {
        MQTTTransport::instance()->publishTelemetry("{\"event\":\"plugin_registry\",\"list\":" + list + "}");
      }
    #endif
    
    // Mesh broadcast (Summary packet)
    ControlPacket reg = ControlPacket::makeTelemetry(
      static_cast<uint16_t>(NVSConfig::getNodeId().toInt()), 0xFE, 0, 0, 0, 0 // 0xFE = Registry Event
    );
    messageRouter.broadcastPacket((uint8_t*)&reg, sizeof(reg));
}

void ProductManager::_ensureDir() {
    if (!LittleFS.exists("/products")) {
        LittleFS.mkdir("/products");
    }
}

String ProductManager::_productPath(const String& name) {
    return "/products/" + name + ".json";
}

bool ProductManager::saveProduct(const String& name, const String& json) {
    if (name.isEmpty() || json.isEmpty()) return false;

    _ensureDir();
    File f = LittleFS.open(_productPath(name), "w");
    if (!f) return false;
    f.print(json);
    f.close();
    return true;
}

bool ProductManager::loadProduct(const String& name) {
    String path = _productPath(name);
    if (!LittleFS.exists(path)) return false;

    File f = LittleFS.open(path, "r");
    String json = f.readString();
    f.close();

    ArduinoJson::StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc["pins"].is<JsonArray>()) _applyPins(doc["pins"]);
    if (doc["schedules"].is<JsonArray>()) _applySchedules(doc["schedules"]);
    // Alerts restoration planned for Phase 6 complement

    _activeProduct = name;
    NVSConfig::setActiveProductName(name);
    
    // Announce over Mesh
    ControlPacket announce = ControlPacket::makeTelemetry(
      static_cast<uint16_t>(NVSConfig::getNodeId().toInt()), 0xFF, 0, 0, 0, 0
    );
    // Future: Add specific "Product Announce" type, for now use piggybacked telemetry
    messageRouter.broadcastPacket((uint8_t*)&announce, sizeof(announce));
    
    // Announce via MQTT
    #ifdef ENABLE_MQTT_TRANSPORT
      if (MQTTTransport::instance()->isConnected()) {
        MQTTTransport::instance()->publishTelemetry("{\"event\":\"product_load\",\"name\":\"" + name + "\"}");
      }
    #endif

    return true;
}

void ProductManager::restoreActiveProduct() {
    String active = NVSConfig::getActiveProductName();
    if (!active.isEmpty()) {
        loadProduct(active);
    }
}

String ProductManager::listProducts() {
    _ensureDir();
    String out = "[";
    bool first = true;
    File root = LittleFS.open("/products");
    if (!root) return "[]";
    
    File f = root.openNextFile();
    while (f) {
        String fname = String(f.name());
        if (fname.endsWith(".json")) {
            if (!first) out += ",";
            out += "\"" + fname.substring(0, fname.length() - 5) + "\"";
            first = false;
        }
        f = root.openNextFile();
    }
    out += "]";
    return out;
}

void ProductManager::_applyPins(const JsonArray& pins) {
    for (JsonObjectConst p : pins) {
        int pinNum = p["pin"] | -1;
        String mode = p["mode"] | "output";
        if (pinNum < 0) continue;

        if (_isPinProtected(pinNum)) {
            Serial.printf("[ProductManager] REJECTED protected pin: %d\n", pinNum);
            continue;
        }

        pinMode(pinNum, (mode == "input") ? INPUT : OUTPUT);
        if (mode == "output" && p.containsKey("default")) {
            digitalWrite(pinNum, p["default"].as<int>());
        }
    }
}

void ProductManager::_applySchedules(const JsonArray& schedules) {
    for (JsonObjectConst s : schedules) {
        String id = s["id"] | "";
        String type = s["type"] | "TOGGLE";
        int pin = s["pin"] | -1;
        unsigned long interval = s["interval"] | 60;
        unsigned long duration = s["duration"] | 0;

        if (id.isEmpty() || pin < 0) continue;
        ScheduleManager::addTask(id, type, pin, interval, duration, "PRODUCT");
    }
}

bool ProductManager::_isPinProtected(int pin) {
    if (pin < 0) return true;
    if (pin >= MCP_PIN_BASE) return false; // MCP pins are external expansion

    // Forbidden ESP32 Native Pins (Based on board_config.h)
    static const int protectedPins[] = {
        LORA_MOSI, LORA_MISO, LORA_SCLK, LORA_CS, LORA_RESET, LORA_DIO1,
#ifdef LORA_BUSY
        LORA_BUSY,
#endif
        I2C_SDA, I2C_SCL, OLED_RESET_PIN,
        BAT_ADC_PIN, GPIO_VEXT, BUTTON_PIN,
        SERIAL_RX, SERIAL_TX
    };

    for (int p : protectedPins) {
        if (p >= 0 && pin == p) return true;
    }
    return false;
}
