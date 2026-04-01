#include "wifi_mx_adapter.h"
#include "command_manager.h"
#include "../Transport/wifi_transport.h"
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

    m_initialized = true;
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
    // Safety Checklist: Post to queue with 0 tick timeout (Non-blocking)
    return m_queue.post(msg);
}
