#include "serial_transport.h"
#include "../App/command_mx_bridge.h"
#include <Arduino.h>

SerialTransport& SerialTransport::getInstance() {
    static SerialTransport instance;
    return instance;
}

SerialTransport& serialTransport = SerialTransport::getInstance();

bool SerialTransport::init() {
    // Serial.begin() is already called in main.cpp setup()
    return true;
}

void SerialTransport::shutdown() {
}

bool SerialTransport::isReady() const {
    return true; // Serial is always ready if connected
}

int SerialTransport::send(const uint8_t* payload, size_t len) {
    if (len == 0) return 0;
    return Serial.write(payload, len);
}

int SerialTransport::recv(uint8_t* buffer, size_t maxLen) {
    // Human-readable CLI doesn't use raw binary recv
    return -1;
}

bool SerialTransport::isAvailable() const {
    return Serial.available() > 0;
}

void SerialTransport::poll() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (_rxBuffer.length() > 0) {
                _rxBuffer.trim();
                Serial.printf("\n[SERIAL] Processing: '%s'\n", _rxBuffer.c_str());
                
                // Route directly to CommandMxBridge for human-readable processing
                CommandMxBridge::process(_rxBuffer, [](const String& response) {
                    Serial.print("\n> ");
                    Serial.println(response);
                    Serial.print("AG> "); 
                });
                
                _rxBuffer = "";
            }
        } else {
            // Echo character (optional, but helpful for human interface)
            Serial.write(c);
            _rxBuffer += c;
        }
    }
}

const char* SerialTransport::getStatus() const {
    return Serial ? "Connected" : "Disconnected";
}

void SerialTransport::sendString(const String& data) {
    Serial.println(data);
}
