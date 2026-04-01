#pragma once
#include "mx_message.h"

/**
 * MxConsumer — Interface for any component that receives messages from the Mx bus.
 */
class MxConsumer {
public:
    virtual ~MxConsumer() = default;

    /**
     * consume — Process a message. Called by the owning task's run loop.
     * @param msg The message to process.
     * @return true if consumed successfully.
     */
    virtual bool consume(const MxMessage& msg) = 0;
};
