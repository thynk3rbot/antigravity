#pragma once
#include "../../lib/Transport/interface.h"
#include <vector>
#include <cstring>

class MockTransport : public TransportInterface {
public:
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> nextRecv;

    bool init() override { return true; }
    int  send(const uint8_t* data, size_t len) override {
        sent.push_back(std::vector<uint8_t>(data, data + len));
        return (int)len;
    }
    int  recv(uint8_t* buf, size_t maxLen) override {
        if (nextRecv.empty()) return 0;
        size_t n = nextRecv.size() < maxLen ? nextRecv.size() : maxLen;
        memcpy(buf, nextRecv.data(), n);
        nextRecv.clear();
        return (int)n;
    }
    bool isReady() const override { return true; }
    bool isAvailable() const override { return !nextRecv.empty(); }
    void poll() override {}
    TransportType getType() const override { return TransportType::SERIAL_DEBUG; }
    const char* getName() const override { return "MockTransport"; }
    const char* getStatus() const override { return "Mock OK"; }
};
