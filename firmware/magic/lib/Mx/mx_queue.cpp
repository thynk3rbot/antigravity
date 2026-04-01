#include "mx_queue.h"
#include "mx_pool.h"
#include <cstring>

MxQueue::MxQueue(const char* name, uint8_t depth) 
    : m_name(name) {
    m_queue = xQueueCreate(depth, sizeof(MxMessage*));
}

MxQueue::~MxQueue() {
    vQueueDelete(m_queue);
}

bool MxQueue::post(const MxMessage& msg) {
    MxMessage* slot = MxPool::instance().alloc();
    if (!slot) return false;

    std::memcpy(slot, &msg, sizeof(MxMessage));

    if (xQueueSend(m_queue, &slot, 0) != pdTRUE) {
        MxPool::instance().release(slot);
        return false;
    }
    return true;
}

bool MxQueue::postFromISR(const MxMessage& msg, BaseType_t* woken) {
    MxMessage* slot = MxPool::instance().allocFromISR();
    if (!slot) return false;

    std::memcpy(slot, &msg, sizeof(MxMessage));

    if (xQueueSendFromISR(m_queue, &slot, woken) != pdTRUE) {
        MxPool::instance().releaseFromISR(slot);
        return false;
    }
    return true;
}

MxMessage* MxQueue::receive(uint32_t timeout_ms) {
    MxMessage* msg = nullptr;
    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(m_queue, &msg, ticks) == pdTRUE) {
        return msg;
    }
    return nullptr;
}

void MxQueue::release(MxMessage* msg) {
    if (msg) {
        MxPool::instance().release(msg);
    }
}

uint8_t MxQueue::pending() const {
    return uxQueueMessagesWaiting(m_queue);
}
