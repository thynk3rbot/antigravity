#pragma once
#include <Arduino.h>
#include <functional>

class CommandManager {
public:
    // Response callback: transport sends this string back to caller
    using ResponseCallback = std::function<void(const String& response)>;

    // Relay control callback
    using RelayCallback = std::function<void(uint8_t relay, bool state)>;

    static void begin();

    // Process a command string, call responseCallback with result
    static void process(const String& input, ResponseCallback responseCallback);

    // Set relay control callback (called when RELAY command received)
    static void setRelayCallback(RelayCallback cb);

    // Status data for STATUS command response
    struct StatusData {
        String nodeId;
        String version;
        String ipAddr;
        float batVoltage;
        uint8_t batPercent;
        String powerMode;
        bool relay1;
        bool relay2;
        int loraRSSI;
        float loraSNR;
        uint32_t loraTX;
        uint32_t loraRX;
        uint8_t meshNeighbors;
        uint32_t uptime;
        uint32_t freeHeap;
    };
    static void updateStatus(const StatusData& data);

private:
    static RelayCallback _relayCallback;
    static StatusData _lastStatus;

    static String _handleStatus();
    static String _handleRelay(const String& args);
    static String _handleSetWifi(const String& args);
    static String _handleBlink();
    static String _handleReboot();
    static String _handleHelp();
    static String _handleGetConfig();
    static String _handleSetName(const String& args);
    static String _handleSetIP(const String& args);
    static String _handleSched(const String& args);
    static String _handleFactoryReset();
    static String _handleAsk(const String& args);
    static String _handleForward(const String& args);
    static String _handleGPS(const String& args);

    // Parse "CMD ARGS" -> cmd and args
    static void _parseCommand(const String& input, String& cmd, String& args);
};
