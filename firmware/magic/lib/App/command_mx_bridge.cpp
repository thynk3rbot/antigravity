#include "command_mx_bridge.h"
#include <Arduino.h>
#include <algorithm>

using std::min;

// Use global MX_PAYLOAD_MAX from mx_message.h

void CommandMxBridge::process(const String& input, CommandManager::ResponseCallback callback) {
    // 1. Publish inbound command to bus (EXECUTE on COMMAND subject)
    MxMessage cmdMsg;
    memset(&cmdMsg, 0, sizeof(cmdMsg)); // Safety: zero the message
    cmdMsg.op = MxOp::EXECUTE;
    cmdMsg.subject_id = MxSubjects::COMMAND;
    cmdMsg.payload_len = min((size_t)input.length(), MX_PAYLOAD_MAX);
    memcpy(cmdMsg.payload, input.c_str(), cmdMsg.payload_len);
    MxBus::instance().publish(cmdMsg);

    // 2. Call original CommandManager — response goes to caller's callback.
    //    We capture it locally to broadcast it to the bus.
    String capturedResponse;
    CommandManager::process(input, [&capturedResponse, &callback](const String& response) {
        capturedResponse = response;
        if (callback) callback(response); // Original callback still fires
    });

    // 3. Publish response to bus (UPDATE on COMMAND_REPLY subject)
    if (capturedResponse.length() > 0) {
        MxMessage replyMsg;
        memset(&replyMsg, 0, sizeof(replyMsg)); // Safety: zero the message
        replyMsg.op = MxOp::UPDATE;
        replyMsg.subject_id = MxSubjects::COMMAND_REPLY;
        replyMsg.payload_len = min((size_t)capturedResponse.length(), MX_PAYLOAD_MAX);
        memcpy(replyMsg.payload, capturedResponse.c_str(), replyMsg.payload_len);
        MxBus::instance().publish(replyMsg);
    }
}
