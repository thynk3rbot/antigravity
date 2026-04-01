#include "mx_pool.h"
#include <cstring>

MxPool& MxPool::instance() {
    static MxPool inst;
    return inst;
}

MxPool::MxPool() {
    m_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MX_POOL_SIZE; i++) {
        m_free[i] = true;
    }
}

MxMessage* MxPool::alloc() {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return nullptr;
    }

    MxMessage* found = nullptr;
    for (int i = 0; i < MX_POOL_SIZE; i++) {
        if (m_free[i]) {
            m_free[i] = false;
            found = &m_slots[i];
            break;
        }
    }

    xSemaphoreGive(m_mutex);
    return found;
}

MxMessage* MxPool::allocFromISR() {
    MxMessage* found = nullptr;
    portENTER_CRITICAL_ISR(&m_spinlock);
    
    for (int i = 0; i < MX_POOL_SIZE; i++) {
        if (m_free[i]) {
            m_free[i] = false;
            found = &m_slots[i];
            break;
        }
    }

    portEXIT_CRITICAL_ISR(&m_spinlock);
    return found;
}

void MxPool::release(MxMessage* slot) {
    if (!slot) return;

    // Determine index
    intptr_t ptr = reinterpret_cast<intptr_t>(slot);
    intptr_t start = reinterpret_cast<intptr_t>(&m_slots[0]);
    int index = (ptr - start) / sizeof(MxMessage);

    if (index < 0 || index >= MX_POOL_SIZE) return;

    // Safety: use the same spinlock for release to ensure consistency with ISR alloc
    portENTER_CRITICAL(&m_spinlock);
    m_free[index] = true;
    portEXIT_CRITICAL(&m_spinlock);
}

void MxPool::releaseFromISR(MxMessage* slot) {
    if (!slot) return;

    intptr_t ptr = reinterpret_cast<intptr_t>(slot);
    intptr_t start = reinterpret_cast<intptr_t>(&m_slots[0]);
    int index = (ptr - start) / sizeof(MxMessage);

    if (index < 0 || index >= MX_POOL_SIZE) return;

    portENTER_CRITICAL_ISR(&m_spinlock);
    m_free[index] = true;
    portEXIT_CRITICAL_ISR(&m_spinlock);
}

uint8_t MxPool::available() const {
    uint8_t count = 0;
    for (int i = 0; i < MX_POOL_SIZE; i++) {
        if (m_free[i]) count++;
    }
    return count;
}
