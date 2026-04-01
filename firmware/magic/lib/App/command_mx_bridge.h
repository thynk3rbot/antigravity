#pragma once
#include "command_manager.h"
#include "../Mx/mx_bus.h"
#include "../Mx/mx_message.h"
#include "../Mx/mx_subjects.h"

/**
 * CommandMxBridge — Wraps CommandManager to publish commands and responses
 * to MxBus. Drop-in replacement for CommandManager::process() calls.
 */
class CommandMxBridge {
public:
    /**
     * @brief Drop-in replacement for CommandManager::process().
     * This method is synchronous: calls the manager, captures the response,
     * and publishes both to the Mx bus.
     */
    static void process(const String& input, CommandManager::ResponseCallback callback);
};
