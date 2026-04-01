#include <Arduino.h>
#include "interface.h"
#include "../App/command_manager.h"

/**
 * @class SerialTransport
 * @brief Human-readable Serial CLI transport
 * 
 * Provides a command-line interface over USB Serial (115200 baud).
 * Directly interfaces with CommandManager for local actions.
 */
class SerialTransport : public TransportInterface {
public:
    static SerialTransport& getInstance();

    bool init() override;
    void shutdown() override;
    bool isReady() const override;
    int send(const uint8_t* payload, size_t len) override;
    int recv(uint8_t* buffer, size_t maxLen) override;
    bool isAvailable() const override;
    void poll() override;

    const char* getName() const override { return "Serial"; }
    TransportType getType() const override { return TransportType::SERIAL_CLI; }
    const char* getStatus() const override;

    // Direct string interface for CLI
    void sendString(const String& data);

private:
    SerialTransport() = default;
    String _rxBuffer;
};

extern SerialTransport& serialTransport;
