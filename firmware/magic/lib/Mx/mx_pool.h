#pragma once
#include "mx_message.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * MxPool — Static pre-allocated pool for MxMessage objects.
 * Guarantees zero heap allocation for internal messaging.
 */
constexpr uint8_t MX_POOL_SIZE = 16;

class MxPool {
public:
    static MxPool& instance();

    // Get a free slot. Returns nullptr if exhausted.
    MxMessage* alloc();

    // ISR-safe alloc (uses critical sections for multi-core ESP32)
    MxMessage* allocFromISR();

    // Return slot to pool
    void release(MxMessage* slot);

    // ISR-safe release (uses portENTER_CRITICAL_ISR)
    void releaseFromISR(MxMessage* slot);

    uint8_t available() const;

private:
    MxPool();
    MxMessage m_slots[MX_POOL_SIZE];
    bool m_free[MX_POOL_SIZE];          // true = available
    SemaphoreHandle_t m_mutex;
    portMUX_TYPE m_spinlock = portMUX_INITIALIZER_UNLOCKED;
};
