#include "mx_bus.h"

MxBus& MxBus::instance() {
    static MxBus inst;
    return inst;
}

bool MxBus::subscribe(uint16_t subject_id, MxConsumer* consumer, MxQueue* queue) {
    for (auto& sub : m_subs) {
        if (sub.subject_id == 0) {
            sub.subject_id = subject_id;
            sub.consumer = consumer;
            sub.queue = queue;
            return true;
        }
    }
    return false;
}

bool MxBus::unsubscribe(uint16_t subject_id, MxConsumer* consumer) {
    for (auto& sub : m_subs) {
        if (sub.subject_id == subject_id && sub.consumer == consumer) {
            sub.subject_id = 0;
            sub.consumer = nullptr;
            sub.queue = nullptr;
            return true;
        }
    }
    return false;
}

uint8_t MxBus::publish(const MxMessage& msg) {
    uint8_t count = 0;
    for (auto& sub : m_subs) {
        if (sub.subject_id == msg.subject_id && sub.queue) {
            if (sub.queue->post(msg)) count++;
        }
    }
    return count;
}

uint8_t MxBus::publishFromISR(const MxMessage& msg, BaseType_t* woken) {
    uint8_t count = 0;
    for (auto& sub : m_subs) {
        if (sub.subject_id == msg.subject_id && sub.queue) {
            if (sub.queue->postFromISR(msg, woken)) count++;
        }
    }
    return count;
}
