#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "mx_message.h"

class MxQueue {
public:
    MxQueue(const char* name, uint8_t depth);
    ~MxQueue();

    // Post a message (copies into pool slot, enqueues pointer)
    // Returns false if pool exhausted or queue full
    bool post(const MxMessage& msg);

    // Post from ISR context (RadioLib callback)
    bool postFromISR(const MxMessage& msg, BaseType_t* woken);

    // Blocking receive — waits up to timeout_ms
    // Caller gets a pool slot pointer. MUST call release() when done.
    MxMessage* receive(uint32_t timeout_ms = portMAX_DELAY);

    // Return a message slot to the pool
    void release(MxMessage* msg);

    uint8_t pending() const;
    const char* name() const { return m_name; }

private:
    QueueHandle_t m_queue;
    const char* m_name;
};
