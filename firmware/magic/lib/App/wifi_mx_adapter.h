#pragma once
#include "../Mx/mx_bus.h"
#include "../Mx/mx_consumer.h"
#include "../Mx/mx_queue.h"
#include "../Mx/mx_subjects.h"

/**
 * WiFiMxAdapter — Active object adapter for WiFiTransport.
 * Bridges the Mx message bus to the WiFi task, preventing lock contention.
 */
class WiFiMxAdapter : public MxConsumer {
public:
    static WiFiMxAdapter& instance();

    /**
     * @brief Initialize the adapter (create queue, subscribe to bus)
     */
    void init();

    /**
     * @brief Drain the adapter's queue — call from WiFiTask loop only.
     * Limits work to 4 messages per cycle to prevent starving OTA/mDNS.
     */
    void drainQueue();

    /**
     * @brief MxConsumer interface — receive messages from MxBus.
     * Non-blocking post to the internal @m_queue.
     */
    bool consume(const MxMessage& msg) override;

private:
    WiFiMxAdapter() : m_initialized(false) {}
    ~WiFiMxAdapter() {}

    void _publishMqtt(const MxMessage& msg);

    bool m_initialized;
    MxQueue m_queue{"wifi", 8};
};
