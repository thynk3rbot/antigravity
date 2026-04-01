#pragma once
#include "mx_bus.h"
#include "mx_pool.h"

/**
 * MxSystem — Global framework orchestrator.
 */
class MxSystem {
public:
    static MxSystem& instance();

    bool init();
    void start();
    void stop();

    // System state
    bool isReady() const { return m_initialized; }

private:
    MxSystem() = default;
    bool m_initialized = false;
};
