/**
 * @file ble_transport.cpp
 * @brief BLE NUS Transport Implementation for LoRaLink v2
 *
 * Provides Nordic UART Service (NUS) for wireless terminal communication.
 * Compatible with ble_instrument.py and standard BLE apps.
 */

#include "ble_transport.h"
#include "../App/nvs_manager.h"
#include <Arduino.h>

// BLE Note: Full BLE NUS implementation requires esp32-nimble library.
// For now, we provide a graceful no-op implementation that initializes
// cleanly but informs the user that BLE is not yet available on this platform.
// To enable full BLE support in the future:
//  1. Add "esp32-nimble @ ^1.5.0" to platformio.ini lib_deps
//  2. Uncomment the includes below and implement NUS GATT callbacks
//
// #if defined(ARDUINO_HELTEC_WIFI_LORA_32_V3) || defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
//   #include <NimBLEDevice.h>
//   #include <NimBLEServer.h>
//   #include <NimBLEUtils.h>
//   #include <NimBLECharacteristic.h>
//   #define BLE_AVAILABLE 1
// #else
//   #define BLE_AVAILABLE 0
// #endif

// For now, stub implementation
#define BLE_AVAILABLE 0

// ============================================================================
// Static Member Initialization
// ============================================================================

bool BLETransport::initialized = false;
bool BLETransport::connected = false;
uint8_t BLETransport::rxBuffer[BLETransport::RX_BUFFER_SIZE];
uint16_t BLETransport::rxPos = 0;
uint8_t BLETransport::txBuffer[BLETransport::TX_BUFFER_SIZE];
uint16_t BLETransport::txPos = 0;
uint32_t BLETransport::txBytes = 0;
uint32_t BLETransport::rxBytes = 0;
int BLETransport::lastError = 0;
char BLETransport::statusString[32] = "Disconnected";

BLETransport::ConnectionCallback BLETransport::connectCallback = nullptr;
BLETransport::ConnectionCallback BLETransport::disconnectCallback = nullptr;

#if BLE_AVAILABLE
  static BluetoothSerial SerialBT;
#endif

// ============================================================================
// Static Public Methods
// ============================================================================

bool BLETransport::initStatic() {
#if BLE_AVAILABLE
    if (initialized) {
        return true;
    }

    // Get device name from NVS (e.g., "Peer1")
    std::string nodeID = NVSManager::getNodeID("Unknown");
    std::string deviceName = "GW-" + nodeID;

    Serial.printf("[BLE] Initializing with name: %s\n", deviceName.c_str());

    // Initialize BluetoothSerial (simpler than bare NimBLE/BLE API)
    // BluetoothSerial uses SPP (Serial Port Profile) which is simpler for terminal
    // but we'll use it as an implementation until custom GATT is needed.
    if (!SerialBT.begin(deviceName.c_str())) {
        Serial.println("[BLE] BluetoothSerial initialization failed");
        lastError = -1;
        snprintf(statusString, sizeof(statusString), "Init Error");
        return false;
    }

    initialized = true;
    snprintf(statusString, sizeof(statusString), "Advertising");
    Serial.println("[BLE] BLE initialized and advertising");

    return true;
#else
    Serial.println("[BLE] BLE not supported on this hardware");
    lastError = -100;  // INIT_ERROR
    snprintf(statusString, sizeof(statusString), "Not Supported");
    return false;
#endif
}


bool BLETransport::isConnected() {
#if BLE_AVAILABLE
    if (!initialized) {
        return false;
    }

    // BluetoothSerial::hasClient() returns true if a client is connected
    return SerialBT.hasClient();
#else
    return false;
#endif
}

bool BLETransport::send(const uint8_t* data, uint16_t len) {
#if BLE_AVAILABLE
    if (!initialized || data == nullptr || len == 0) {
        lastError = -6;  // INVALID_ARG
        return false;
    }

    // If not connected, queue anyway (will be sent on connect if buffer allows)
    // For now, we'll just write directly to BluetoothSerial

    size_t written = SerialBT.write(data, len);
    if (written > 0) {
        txBytes += written;
        return written == len;
    }

    lastError = -2;  // TX_BUSY
    return false;
#else
    return false;
#endif
}

int BLETransport::recvImpl(uint8_t* buffer, size_t maxLen) {
#if BLE_AVAILABLE
    if (!initialized || buffer == nullptr || maxLen == 0) {
        return (int)TransportStatus::INVALID_ARG;
    }

    // Check if data available from BluetoothSerial
    if (SerialBT.available()) {
        size_t bytesRead = SerialBT.read();
        if (bytesRead >= 0) {
            // Add to RX buffer
            if (rxPos < RX_BUFFER_SIZE) {
                rxBuffer[rxPos++] = (uint8_t)bytesRead;
                rxBytes++;
            }
        }
    }

    // Try to extract a complete line (newline-terminated)
    uint16_t lineLen = 0;
    if (getNextLine(buffer, lineLen)) {
        return lineLen;
    }

    return 0;  // No complete line available
#else
    return (int)TransportStatus::NOT_READY;
#endif
}

bool BLETransport::isAvailable() const {
#if BLE_AVAILABLE
    if (!initialized) {
        return false;
    }

    // Check if there's a complete line in the buffer
    for (uint16_t i = 0; i < rxPos; i++) {
        if (rxBuffer[i] == '\n') {
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

bool BLETransport::getNextLine(uint8_t* outLine, uint16_t& outLen) {
    if (outLine == nullptr || rxPos == 0) {
        outLen = 0;
        return false;
    }

    // Search for newline
    for (uint16_t i = 0; i < rxPos; i++) {
        if (rxBuffer[i] == '\n') {
            // Found complete line (0 to i inclusive)
            uint16_t lineLength = i + 1;
            if (lineLength <= RX_BUFFER_SIZE) {
                memcpy(outLine, rxBuffer, lineLength);
                outLen = lineLength;

                // Shift remaining data
                if (i + 1 < rxPos) {
                    memmove(rxBuffer, rxBuffer + i + 1, rxPos - i - 1);
                }
                rxPos -= (i + 1);

                return true;
            }
        }
    }

    // No newline found
    outLen = 0;
    return false;
}

void BLETransport::pollStatic() {
#if BLE_AVAILABLE
    if (!initialized) {
        return;
    }

    // Check connection status
    bool wasConnected = connected;
    connected = isConnected();

    if (connected && !wasConnected) {
        // Just connected
        snprintf(statusString, sizeof(statusString), "Connected");
        if (connectCallback) {
            connectCallback();
        }
        Serial.println("[BLE] Client connected");
    } else if (!connected && wasConnected) {
        // Just disconnected
        snprintf(statusString, sizeof(statusString), "Advertising");
        if (disconnectCallback) {
            disconnectCallback();
        }
        Serial.println("[BLE] Client disconnected");
        // Clear buffers on disconnect
        rxPos = 0;
    } else if (!connected) {
        snprintf(statusString, sizeof(statusString), "Advertising");
    }

    // Process any received data (non-blocking read from BluetoothSerial)
    while (SerialBT.available()) {
        int byte = SerialBT.read();
        if (byte >= 0 && rxPos < RX_BUFFER_SIZE) {
            rxBuffer[rxPos++] = (uint8_t)byte;
            rxBytes++;
        }
    }
#endif
}

void BLETransport::poll() {
    pollStatic();
}

void BLETransport::onConnect(ConnectionCallback callback) {
    connectCallback = callback;
}

void BLETransport::onDisconnect(ConnectionCallback callback) {
    disconnectCallback = callback;
}

uint16_t BLETransport::getMTU() {
    return BLE_MTU;
}

void BLETransport::shutdownStatic() {
#if BLE_AVAILABLE
    if (initialized) {
        SerialBT.end();
        initialized = false;
        connected = false;
        rxPos = 0;
        snprintf(statusString, sizeof(statusString), "Stopped");
        Serial.println("[BLE] BLE shutdown");
    }
#endif
}

void BLETransport::shutdown() {
    shutdownStatic();
}

const char* BLETransport::getStatusString() {
    return statusString;
}

const char* BLETransport::getLastErrorString() const {
    switch (lastError) {
        case 0:
            return "No error";
        case -1:
            return "Not ready";
        case -2:
            return "TX busy";
        case -3:
            return "RX timeout";
        case -5:
            return "Buffer full";
        case -6:
            return "Invalid argument";
        case -100:
            return "Init error";
        default:
            return "Unknown error";
    }
}

// ============================================================================
// TransportInterface Implementation (Virtual Methods)
// ============================================================================

bool BLETransport::init() {
    return initStatic();
}

bool BLETransport::isReady() const {
    return initialized;
}

int BLETransport::send(const uint8_t* payload, size_t len) {
    if (BLETransport::send(payload, (uint16_t)len)) {
        return (int)len;
    }
    return lastError;
}

int BLETransport::recv(uint8_t* buffer, size_t maxLen) {
    return recvImpl(buffer, maxLen);
}

// Note: poll() is already defined above as virtual method delegate

// ============================================================================
// Internal Callback Handlers
// ============================================================================

void BLETransport::bleOnConnect() {
    BLETransport::connected = true;
}

void BLETransport::bleOnDisconnect() {
    BLETransport::connected = false;
}

void BLETransport::bleOnRxData(const uint8_t* data, uint16_t len) {
    if (data && len > 0 && rxPos + len <= RX_BUFFER_SIZE) {
        memcpy(rxBuffer + rxPos, data, len);
        rxPos += len;
        rxBytes += len;
    }
}

void BLETransport::bleProcessTxQueue() {
    // Currently handled by BluetoothSerial directly
}
