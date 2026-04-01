#include "wifi_mx_adapter.h"
#include "command_manager.h"
#include "../Transport/wifi_transport.h"
#include "../Transport/mqtt_transport.h"
#include <ArduinoJson.h>
#include <esp_log.h>

static const char* TAG = "WiFiMxAdapter";

WiFiMxAdapter& WiFiMxAdapter::instance() {
    static WiFiMxAdapter inst;
    return inst;
}

void WiFiMxAdapter::init() {
    if (m_initialized) return;

    ESP_LOGI(TAG, "Initializing WiFi Mx Adapter...");
    
    // Subscribe to commands and node status updates
    MxBus::instance().subscribe(MxSubjects::COMMAND, this, &m_queue);
    MxBus::instance().subscribe(MxSubjects::NODE_STATUS, this, &m_queue);
    MxBus::instance().subscribe(MxSubjects::GPS_POSITION, this, &m_queue);

    m_initialized = true;
}

void WiFiMxAdapter::_publishMqtt(const MxMessage& msg) {
    if (!WiFiTransport::isConnected()) return;

    // Convert Mx subjects to MQTT topics: magic/{nodeId}/mx/{subject_name}
    String subjectName = "unknown";
    if (msg.subject_id == MxSubjects::NODE_STATUS) subjectName = "status";
    else if (msg.subject_id == MxSubjects::GPS_POSITION) subjectName = "gps";
    else return; // Only mirror telemetry subjects

    String topic = "magic/" + MQTTTransport::instance()->getNodeId() + "/mx/" + subjectName;

    // Use JSON for MQTT observability (compatible with Telegraf)
    StaticJsonDocument<512> doc;
    doc["node_id"] = MQTTTransport::instance()->getNodeId();
    doc["subject_id"] = msg.subject_id;
    doc["op"] = (uint8_t)msg.op;
    
    // Add common record fields based on payload if possible
    // Note: The binary record is passed for parsing in Telegraf,
    // but we can provide the raw bytes or a base64 version here.
    // For now, we'll just push the "update" trigger.
    doc["updated"] = millis();

    String out;
    serializeJson(doc, out);
    MQTTTransport::instance()->publishTelemetry(out);
}

void WiFiMxAdapter::drainQueue() {
    // Lazy init for adherence to "no modification to boot_sequence" rule
    if (!m_initialized) {
        init();
    }

    // Process up to 4 messages per tick to prevent starving OTA/mDNS
    uint8_t processed = 0;
    while (processed < 4) {
        MxMessage* msg = m_queue.receive(0); // Non-blocking check
        if (!msg) break;

        switch (msg->subject_id) {
            case MxSubjects::COMMAND:
                if (msg->op == MxOp::EXECUTE) {
                    // Route to CommandManager (cast payload to String safe as it's null-terminated or len-bounded)
                    String cmdStr((char*)msg->payload, msg->payload_len);
                    ESP_LOGD(TAG, "Executing command: %s", cmdStr.c_str());
                    CommandManager::process(cmdStr, nullptr);
                }
                break;

            case MxSubjects::NODE_STATUS:
                if (msg->op == MxOp::UPDATE) {
                    // Placeholder for actual HTTP API cache update (Phase 2 extension)
                    ESP_LOGD(TAG, "Status update received");
                }
                break;

            default:
                break;
        }

        // Release the message back to pool
        m_queue.release(msg);
        processed++;
    }
}

bool WiFiMxAdapter::consume(const MxMessage& msg) {
    // 1. Mirror to MQTT Infrastructure immediately (Production Bridge)
    _publishMqtt(msg);

    // 2. Post to internal local queue for secondary processing
    return m_queue.post(msg);
}
