/**
 * @file ble_transport.h
 * @brief Bluetooth Low Energy (BLE) NUS Transport for LoRaLink v2
 *
 * Implements Nordic UART Service (NUS) for wireless communication with:
 * - Mobile apps (iOS/Android BLE scanners)
 * - ble_instrument.py test harness
 * - Custom BLE clients
 *
 * Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (Nordic UART Service)
 * TX (notify):  6E400002-B5A3-F393-E0A9-E50E24DCCA9E
 * RX (write):   6E400003-B5A3-F393-E0A9-E50E24DCCA9E
 *
 * Features:
 * - Device name: "GW-{NODEID}" (e.g., "GW-Peer1")
 * - Single concurrent connection (peripheral mode)
 * - Automatic fragmentation/reassembly for > 20 bytes
 * - Line-buffering for command processing (newline-terminated)
 * - Non-blocking TX queue with notifications
 * - Graceful connection/disconnection handling
 */

#pragma once

#include "interface.h"
#include <string>
#include <cstdint>
#include <cstring>

/**
 * @class BLETransport
 * @brief Bluetooth Low Energy NUS (Nordic UART Service) transport layer
 *
 * Provides peer-to-peer BLE communication compatible with ble_instrument.py
 * and standard BLE terminal applications.
 */
class BLETransport : public TransportInterface {
public:
    // ========================================================================
    // Initialization & Control
    // ========================================================================

    /**
     * @brief Initialize BLE with NUS service (static version)
     *
     * Sets up BLE stack, creates NUS service, and starts advertising
     * with device name "GW-{NODEID}".
     *
     * @return true if initialization successful, false on error
     */
    static bool initStatic();

    /**
     * @brief Check if BLE client is currently connected
     *
     * @return true if a BLE client is connected, false otherwise
     */
    static bool isConnected();

    /**
     * @brief Send data via BLE NUS TX characteristic (with notifications)
     *
     * Data larger than 20 bytes will be automatically fragmented and sent
     * in multiple notifications. Non-blocking operation - queues data for transmission.
     *
     * @param data Pointer to data buffer
     * @param len Number of bytes to send
     * @return true if data queued successfully, false if queue full
     */
    static bool send(const uint8_t* data, uint16_t len);

    /**
     * @brief Get current MTU size for BLE packets
     *
     * Typically 23 bytes (20-byte payload + 3-byte header for NUS).
     *
     * @return MTU size in bytes
     */
    static uint16_t getMTU();

    /**
     * @brief Shutdown BLE (disconnect, stop advertising) - static version
     *
     * Cleanly closes BLE connection and disables advertising.
     * Device can be re-initialized later.
     */
    static void shutdownStatic();

    /**
     * @brief Get current BLE connection status string
     *
     * @return Status string: "Connected", "Advertising", "Disconnected", "Error"
     */
    static const char* getStatusString();

    // ========================================================================
    // TransportInterface Implementation
    // ========================================================================

    /**
     * @brief Initialize transport (delegates to static init())
     */
    virtual bool init() override;

    /**
     * @brief Check if transport is ready (BLE initialized and/or connected)
     */
    virtual bool isReady() const override;

    /**
     * @brief Send raw bytes via BLE
     *
     * @param payload Pointer to data buffer
     * @param len Number of bytes to send
     * @return Number of bytes sent, or negative TransportStatus code
     */
    virtual int send(const uint8_t* payload, size_t len) override;

    /**
     * @brief Receive raw bytes (non-blocking)
     *
     * Returns data from the RX buffer if available.
     * Data is buffered and presented as complete lines (newline-terminated).
     *
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes received, 0 if no data, or negative TransportStatus code
     */
    virtual int recv(uint8_t* buffer, size_t maxLen) override;

    /**
     * @brief Check if data is available without blocking
     *
     * @return true if a complete line is ready in RX buffer, false otherwise
     */
    virtual bool isAvailable() const override;

    /**
     * @brief Poll the BLE transport (called periodically)
     *
     * Processes pending TX queue and connection events.
     */
    virtual void poll() override;

    /**
     * @brief Shutdown BLE transport
     */
    virtual void shutdown() override;

    /**
     * @brief Poll the BLE transport (static version, called periodically)
     *
     * Processes pending TX queue and connection events.
     */
    static void pollStatic();

    /**
     * @brief Get transport name
     */
    virtual const char* getName() const override { return "BLE"; }

    /**
     * @brief Get transport type
     */
    virtual TransportType getType() const override { return TransportType::BLE; }

    /**
     * @brief Get status string
     */
    virtual const char* getStatus() const override { return getStatusString(); }

    /**
     * @brief Get bytes sent since init
     */
    virtual uint32_t getTxBytes() const override { return txBytes; }

    /**
     * @brief Get bytes received since init
     */
    virtual uint32_t getRxBytes() const override { return rxBytes; }

    /**
     * @brief Get last error code
     */
    virtual int getLastError() const override { return lastError; }

    /**
     * @brief Clear error state
     */
    virtual void clearError() override { lastError = 0; }

    /**
     * @brief Get last error message
     */
    virtual const char* getLastErrorString() const override;

    // ========================================================================
    // BLE Callback Registration (for application integration)
    // ========================================================================

    /**
     * @brief Function pointer type for connection callbacks
     */
    using ConnectionCallback = void (*)(void);

    /**
     * @brief Register callback for BLE connection event
     *
     * @param callback Function to call when client connects
     */
    static void onConnect(ConnectionCallback callback);

    /**
     * @brief Register callback for BLE disconnection event
     *
     * @param callback Function to call when client disconnects
     */
    static void onDisconnect(ConnectionCallback callback);

private:
    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr size_t RX_BUFFER_SIZE = 256;   // Input buffer size
    static constexpr size_t TX_BUFFER_SIZE = 256;   // Output buffer size
    static constexpr size_t TX_QUEUE_SIZE = 4;      // Number of queued TX packets
    static constexpr uint16_t BLE_MTU = 23;         // Max payload: 20 bytes + 3-byte ATT header
    static constexpr uint16_t NUS_PAYLOAD_SIZE = 20; // Max bytes per NUS packet

    // NUS Service UUIDs
    static constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    static constexpr const char* NUS_TX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // Notify
    static constexpr const char* NUS_RX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Write

    // ========================================================================
    // State Management
    // ========================================================================

    static bool initialized;
    static bool connected;
    static uint8_t rxBuffer[RX_BUFFER_SIZE];
    static uint16_t rxPos;
    static uint8_t txBuffer[TX_BUFFER_SIZE];
    static uint16_t txPos;
    static uint32_t txBytes;
    static uint32_t rxBytes;
    static int lastError;
    static char statusString[32];

    static ConnectionCallback connectCallback;
    static ConnectionCallback disconnectCallback;

    // ========================================================================
    // BLE Callback Handlers (internal)
    // ========================================================================

    /**
     * @brief Called when BLE client connects
     */
    static void bleOnConnect();

    /**
     * @brief Called when BLE client disconnects
     */
    static void bleOnDisconnect();

    /**
     * @brief Called when data received on RX characteristic
     *
     * @param data Pointer to received data
     * @param len Number of bytes received
     */
    static void bleOnRxData(const uint8_t* data, uint16_t len);

    /**
     * @brief Called periodically to send queued TX data
     */
    static void bleProcessTxQueue();

    /**
     * @brief Search for newline in RX buffer and extract line
     *
     * @param outLine Pointer to output buffer
     * @param outLen Pointer to store line length (including newline)
     * @return true if complete line found, false otherwise
     */
    static bool getNextLine(uint8_t* outLine, uint16_t& outLen);

    /**
     * @brief Internal receive implementation (static)
     *
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to read
     * @return Number of bytes received, 0 if no data, or negative error code
     */
    static int recvImpl(uint8_t* buffer, size_t maxLen);
};
