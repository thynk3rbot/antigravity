#include "mx_system.h"
#include <esp_log.h>

static const char* TAG = "MxSystem";

MxSystem& MxSystem::instance() {
    static MxSystem inst;
    return inst;
}

bool MxSystem::init() {
    if (m_initialized) return true;

    ESP_LOGI(TAG, "Initializing Mx Framework...");
    
    // Explicitly reference pool and bus singletons to ensure lazy initialization
    MxPool::instance();
    MxBus::instance();

    m_initialized = true;
    return true;
}

void MxSystem::start() {
    if (!m_initialized) init();
    ESP_LOGI(TAG, "Mx Framework started");
}

void MxSystem::stop() {
    ESP_LOGI(TAG, "Mx Framework stopped");
    m_initialized = false;
}
