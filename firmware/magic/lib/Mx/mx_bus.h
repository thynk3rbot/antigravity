#pragma once
#include "mx_message.h"
#include "mx_consumer.h"
#include "mx_queue.h"
#include <array>

/**
 * MxSubscription — Internal tracking for subject registrations.
 */
struct MxSubscription {
    uint16_t subject_id;        // 0 = unused slot
    MxConsumer* consumer;
    MxQueue* queue;             // target queue (consumer's inbox)
};

/**
 * MxBus — Central message dispatcher for the firmware.
 * Routes messages by subject ID to multiple subscriber queues.
 */
constexpr uint8_t MX_MAX_SUBSCRIPTIONS = 32;

class MxBus {
public:
    static MxBus& instance();

    // Subscribe a consumer's queue to a subject
    bool subscribe(uint16_t subject_id, MxConsumer* consumer, MxQueue* queue);
    bool unsubscribe(uint16_t subject_id, MxConsumer* consumer);

    // Publish — copies message to all subscriber queues for this subject
    // Returns number of subscribers reached
    uint8_t publish(const MxMessage& msg);

    // Publish from ISR (RadioLib) — uses postFromISR on target queues
    uint8_t publishFromISR(const MxMessage& msg, BaseType_t* woken);

private:
    MxBus() = default;
    std::array<MxSubscription, MX_MAX_SUBSCRIPTIONS> m_subs{};
};
