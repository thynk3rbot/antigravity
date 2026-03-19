/**
 * @file ble_transport.cpp
 * @brief BLE NUS Transport Implementation for LoRaLink v2
 *
 * Implements Nordic UART Service (NUS) over NimBLE-Arduino.
 * Advertises as "GW-{NODEID}" (compatible with loralink_status.py).
 * RX/TX characteristics for bidirectional string comms.
 * Auto-restarts advertising on disconnect.
 */

#include "ble_transport.h"
#include "../App/nvs_manager.h"
#include <Arduino.h>

#if !defined(NATIVE_TEST)
  #include <NimBLEDevice.h>
  #define BLE_AVAILABLE 1
#else
  #define BLE_AVAILABLE 0
#endif

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

// ============================================================================
// NimBLE GATT Callback Helpers (only compiled when BLE hardware is available)
// ============================================================================

#if BLE_AVAILABLE

static NimBLEServer*         s_pServer   = nullptr;
static NimBLECharacteristic* s_pTxChar   = nullptr;  // Notify  (device→phone)
static NimBLECharacteristic* s_pRxChar   = nullptr;  // Write   (phone→device)

// NUS UUIDs
static const char* NUS_SVC_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_TX_UUID  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Notify
static const char* NUS_RX_UUID  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // Write

// ----------------------------------------------------------------------------
// Server connection callbacks
// ----------------------------------------------------------------------------
class NUSServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        BLETransport::bleOnConnect();
    }
    void onDisconnect(NimBLEServer* pServer) override {
        BLETransport::bleOnDisconnect();
    }
};

// ----------------------------------------------------------------------------
// RX characteristic write callback
// ----------------------------------------------------------------------------
class NUSRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (!value.empty()) {
            BLETransport::bleOnRxData(
                reinterpret_cast<const uint8_t*>(value.data()),
                static_cast<uint16_t>(value.size())
            );
        }
    }
};

static NUSServerCallbacks s_serverCB;
static NUSRxCallbacks     s_rxCB;

#endif  // BLE_AVAILABLE

// ============================================================================
// Static Public Methods
// ============================================================================

bool BLETransport::initStatic() {
#if BLE_AVAILABLE
    if (initialized) {
        return true;
    }

    // Build device name "GW-<nodeId>"
    std::string nodeId     = NVSManager::getNodeID("Unknown");
    std::string deviceName = "GW-" + nodeId;

    Serial.printf("[BLE] Initializing NimBLE NUS as '%s'\n", deviceName.c_str());

    // Init NimBLE stack
    NimBLEDevice::init(deviceName);

    // Create GATT server
    s_pServer = NimBLEDevice::createServer();
    if (!s_pServer) {
        Serial.println("[BLE] Failed to create BLE server");
        lastError = (int)TransportStatus::INIT_ERROR;
        snprintf(statusString, sizeof(statusString), "Init Error");
        return false;
    }
    s_pServer->setCallbacks(&s_serverCB);

    // Create NUS service
    NimBLEService* pService = s_pServer->createService(NUS_SVC_UUID);
    if (!pService) {
        Serial.println("[BLE] Failed to create NUS service");
        lastError = (int)TransportStatus::INIT_ERROR;
        snprintf(statusString, sizeof(statusString), "Init Error");
        return false;
    }

    // TX characteristic: device notifies client (phone reads)
    s_pTxChar = pService->createCharacteristic(
        NUS_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX characteristic: client writes to device
    s_pRxChar = pService->createCharacteristic(
        NUS_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    s_pRxChar->setCallbacks(&s_rxCB);

    // Start the service
    pService->start();

    // Start advertising
    NimBLEAdvertising* pAdvert = NimBLEDevice::getAdvertising();
    pAdvert->addServiceUUID(NUS_SVC_UUID);
    pAdvert->setScanResponse(true);
    pAdvert->start();

    initialized = true;
    snprintf(statusString, sizeof(statusString), "Advertising");
    Serial.println("[BLE] NUS service started, advertising");

    return true;
#else
    Serial.println("[BLE] BLE not available on this platform");
    lastError = (int)TransportStatus::INIT_ERROR;
    snprintf(statusString, sizeof(statusString), "Not Supported");
    return false;
#endif
}

bool BLETransport::isConnected() {
#if BLE_AVAILABLE
    return initialized && connected;
#else
    return false;
#endif
}

bool BLETransport::send(const uint8_t* data, uint16_t len) {
#if BLE_AVAILABLE
    if (!initialized || data == nullptr || len == 0) {
        lastError = (int)TransportStatus::INVALID_ARG;
        return false;
    }
    if (!connected || s_pTxChar == nullptr) {
        lastError = (int)TransportStatus::NOT_READY;
        return false;
    }

    // Fragment into NUS_PAYLOAD_SIZE chunks and notify
    uint16_t offset = 0;
    while (offset < len) {
        uint16_t chunkLen = (len - offset > NUS_PAYLOAD_SIZE)
                            ? NUS_PAYLOAD_SIZE
                            : (len - offset);
        s_pTxChar->setValue(data + offset, chunkLen);
        s_pTxChar->notify();
        offset += chunkLen;
    }

    txBytes += len;
    return true;
#else
    return false;
#endif
}

int BLETransport::recvImpl(uint8_t* buffer, size_t maxLen) {
#if BLE_AVAILABLE
    if (!initialized || buffer == nullptr || maxLen == 0) {
        return (int)TransportStatus::INVALID_ARG;
    }

    uint16_t lineLen = 0;
    if (getNextLine(buffer, lineLen)) {
        return (int)lineLen;
    }
    return 0;
#else
    return (int)TransportStatus::NOT_READY;
#endif
}

bool BLETransport::isAvailable() const {
#if BLE_AVAILABLE
    if (!initialized) return false;
    for (uint16_t i = 0; i < rxPos; i++) {
        if (rxBuffer[i] == '\n') return true;
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

    for (uint16_t i = 0; i < rxPos; i++) {
        if (rxBuffer[i] == '\n') {
            uint16_t lineLength = i + 1;
            memcpy(outLine, rxBuffer, lineLength);
            outLen = lineLength;

            // Shift remaining bytes forward
            if (i + 1 < rxPos) {
                memmove(rxBuffer, rxBuffer + i + 1, rxPos - (i + 1));
            }
            rxPos -= lineLength;
            return true;
        }
    }

    outLen = 0;
    return false;
}

void BLETransport::pollStatic() {
#if BLE_AVAILABLE
    if (!initialized) return;

    // If disconnected, restart advertising so new clients can connect
    if (!connected) {
        NimBLEAdvertising* pAdvert = NimBLEDevice::getAdvertising();
        if (pAdvert && !pAdvert->isAdvertising()) {
            pAdvert->start();
            snprintf(statusString, sizeof(statusString), "Advertising");
        }
    }
    // NimBLE is interrupt-driven; no polling of data needed here
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
        NimBLEDevice::getAdvertising()->stop();
        NimBLEDevice::deinit(true);
        s_pServer  = nullptr;
        s_pTxChar  = nullptr;
        s_pRxChar  = nullptr;
        initialized = false;
        connected   = false;
        rxPos = 0;
        txPos = 0;
        snprintf(statusString, sizeof(statusString), "Stopped");
        Serial.println("[BLE] NimBLE shutdown");
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
        case 0:                                      return "No error";
        case (int)TransportStatus::NOT_READY:        return "Not ready";
        case (int)TransportStatus::TX_BUSY:          return "TX busy";
        case (int)TransportStatus::RX_TIMEOUT:       return "RX timeout";
        case (int)TransportStatus::BUFFER_FULL:      return "Buffer full";
        case (int)TransportStatus::INVALID_ARG:      return "Invalid argument";
        case (int)TransportStatus::INIT_ERROR:       return "Init error";
        default:                                     return "Unknown error";
    }
}

// ============================================================================
// TransportInterface Virtual Method Implementations
// ============================================================================

bool BLETransport::init() {
    return initStatic();
}

bool BLETransport::isReady() const {
    return initialized;
}

int BLETransport::send(const uint8_t* payload, size_t len) {
    if (BLETransport::send(payload, static_cast<uint16_t>(len))) {
        return static_cast<int>(len);
    }
    return lastError;
}

int BLETransport::recv(uint8_t* buffer, size_t maxLen) {
    return recvImpl(buffer, maxLen);
}

// ============================================================================
// Internal BLE Event Handlers (called from NimBLE callbacks)
// ============================================================================

void BLETransport::bleOnConnect() {
    connected = true;
    snprintf(statusString, sizeof(statusString), "Connected");
    Serial.println("[BLE] Client connected");
    if (connectCallback) {
        connectCallback();
    }
}

void BLETransport::bleOnDisconnect() {
    connected = false;
    rxPos = 0;  // Clear RX buffer on disconnect
    snprintf(statusString, sizeof(statusString), "Advertising");
    Serial.println("[BLE] Client disconnected");
    if (disconnectCallback) {
        disconnectCallback();
    }
    // Advertising restart is handled in pollStatic()
}

void BLETransport::bleOnRxData(const uint8_t* data, uint16_t len) {
    if (data == nullptr || len == 0) return;

    // Append to RX ring buffer, dropping overflow bytes
    uint16_t space = RX_BUFFER_SIZE - rxPos;
    uint16_t toCopy = (len <= space) ? len : space;
    if (toCopy > 0) {
        memcpy(rxBuffer + rxPos, data, toCopy);
        rxPos  += toCopy;
        rxBytes += toCopy;
    }

    if (toCopy < len) {
        lastError = (int)TransportStatus::BUFFER_FULL;
        Serial.println("[BLE] RX buffer overflow — bytes dropped");
    }
}

void BLETransport::bleProcessTxQueue() {
    // NimBLE handles TX notifications directly; no separate queue needed.
}
