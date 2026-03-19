/**
 * @file ble_transport.h
 * @brief Bluetooth Low Energy (BLE) NUS Transport for LoRaLink v2
 *
 * Implements Nordic UART Service (NUS) for wireless communication with:
 * - Mobile apps (iOS/Android BLE scanners)
 * - loralink_status.py fleet management tool
 * - ble_instrument.py test harness
 *
 * Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (Nordic UART Service)
 * TX (notify):  6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (device → phone)
 * RX (write):   6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (phone → device)
 *
 * Features:
 * - Device name: "GW-{NODEID}" (e.g., "GW-Peer1") — required by loralink_status.py
 * - Single concurrent connection (peripheral mode)
 * - Automatic fragmentation for payloads > 20 bytes
 * - Line-buffering for newline-terminated command processing
 * - Graceful connection/disconnection handling with advertising restart
 */

#pragma once

#include "interface.h"
#include <string>
#include <cstdint>
#include <cstring>
#include <functional>
#include <Arduino.h>

/**
 * @class BLETransport
 * @brief Bluetooth Low Energy NUS (Nordic UART Service) transport layer
 *
 * Provides peripheral BLE communication compatible with loralink_status.py
 * and standard BLE terminal applications. Uses NimBLE-Arduino stack.
 */
class BLETransport : public TransportInterface {
public:
    // ========================================================================
    // Initialization & Control
    // ========================================================================

    /**
     * @brief Initialize BLE with NUS service (static entry point)
     *
     * Sets up NimBLE stack, creates NUS GATT service, and starts advertising
     * with device name "GW-{NODEID}".
     *
     * @return true if initialization successful, false on error
     */
    static bool initStatic();

    /**
     * @brief Check if a BLE client is currently connected
     * @return true if a BLE client is connected, false otherwise
     */
    static bool isConnected();

    /**
     * @brief Send raw bytes via BLE NUS TX characteristic (with notifications)
     *
     * Data larger than NUS_PAYLOAD_SIZE bytes is automatically fragmented.
     *
     * @param data Pointer to data buffer
     * @param len  Number of bytes to send
     * @return true if sent successfully, false on error or not connected
     */
    static bool send(const uint8_t* data, uint16_t len);

    /**
     * @brief Get current MTU payload size for NUS packets
     * @return Max NUS payload bytes (20)
     */
    static uint16_t getMTU();

    /**
     * @brief Shutdown BLE — disconnect, stop advertising, deinit NimBLE
     */
    static void shutdownStatic();

    /**
     * @brief Get current BLE connection status string
     * @return "Connected", "Advertising", "Stopped", "Init Error", etc.
     */
    static const char* getStatusString();

    /**
     * @brief Process BLE state machine (call from main loop)
     *
     * Restarts advertising if disconnected and not already advertising.
     * NimBLE is interrupt-driven so no data polling is needed here.
     */
    static void pollStatic();

    // ========================================================================
    // TransportInterface Implementation
    // ========================================================================

    /** @brief Initialize transport (delegates to initStatic()) */
    virtual bool init() override;

    /** @brief Check if transport is ready (BLE initialized) */
    virtual bool isReady() const override;

    /**
     * @brief Send raw bytes via BLE NUS
     * @return Bytes sent, or negative TransportStatus code
     */
    virtual int send(const uint8_t* payload, size_t len) override;

    /**
     * @brief Receive raw bytes — returns next complete newline-terminated line
     * @return Bytes received, 0 if no complete line, or negative error code
     */
    virtual int recv(uint8_t* buffer, size_t maxLen) override;

    /** @brief Check if a complete newline-terminated line is ready */
    virtual bool isAvailable() const override;

    /** @brief Poll BLE state machine (delegates to pollStatic()) */
    virtual void poll() override;

    /** @brief Shutdown BLE transport (delegates to shutdownStatic()) */
    virtual void shutdown() override;

    virtual const char*    getName()     const override { return "BLE"; }
    virtual TransportType  getType()     const override { return TransportType::BLE; }
    virtual const char*    getStatus()   const override { return getStatusString(); }
    virtual uint32_t       getTxBytes()  const override { return txBytes; }
    virtual uint32_t       getRxBytes()  const override { return rxBytes; }
    virtual int            getLastError() const override { return lastError; }
    virtual void           clearError()  override       { lastError = 0; }
    virtual const char*    getLastErrorString() const override;

    // ========================================================================
    // Connection Event Callbacks
    // ========================================================================

    using ConnectionCallback = void (*)(void);

    /** @brief Register callback invoked when a BLE client connects */
    static void onConnect(ConnectionCallback callback);

    /** @brief Register callback invoked when a BLE client disconnects */
    static void onDisconnect(ConnectionCallback callback);

    // ========================================================================
    // String-level TX/RX (for CommandManager integration)
    // ========================================================================

    using RxStringCallback = std::function<void(const String&)>;

    /** @brief Send a String over NUS TX characteristic */
    static bool sendStringStatic(const String& str);

    /** @brief Register callback fired when a NUS string is received from client */
    static void setRxCallback(RxStringCallback cb);

    // ========================================================================
    // Internal BLE Event Handlers
    // (public so NimBLE GATT callback classes in ble_transport.cpp can call them)
    // ========================================================================

    /** @brief Called by NimBLE server callback on client connect */
    static void bleOnConnect();

    /** @brief Called by NimBLE server callback on client disconnect */
    static void bleOnDisconnect();

    /**
     * @brief Called by NimBLE RX characteristic callback on write
     * @param data Received bytes
     * @param len  Number of received bytes
     */
    static void bleOnRxData(const uint8_t* data, uint16_t len);

private:
    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr size_t   RX_BUFFER_SIZE   = 256;
    static constexpr size_t   TX_BUFFER_SIZE   = 256;
    static constexpr uint16_t BLE_MTU          = 23;   // ATT MTU: 20-byte payload + 3-byte header
    static constexpr uint16_t NUS_PAYLOAD_SIZE = 20;   // Max bytes per NUS notification

    // ========================================================================
    // Static State
    // ========================================================================

    static bool     initialized;
    static bool     connected;
    static uint8_t  rxBuffer[RX_BUFFER_SIZE];
    static uint16_t rxPos;
    static uint8_t  txBuffer[TX_BUFFER_SIZE];
    static uint16_t txPos;
    static uint32_t txBytes;
    static uint32_t rxBytes;
    static int      lastError;
    static char     statusString[32];
    static RxStringCallback s_rxStringCallback;

    static ConnectionCallback connectCallback;
    static ConnectionCallback disconnectCallback;

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /**
     * @brief Extract next newline-terminated line from rxBuffer
     * @param outLine  Destination buffer
     * @param outLen   Set to line length on success
     * @return true if a complete line was extracted
     */
    static bool getNextLine(uint8_t* outLine, uint16_t& outLen);

    /**
     * @brief Internal recv implementation
     */
    static int recvImpl(uint8_t* buffer, size_t maxLen);

    /**
     * @brief Process TX queue (placeholder; NimBLE handles TX directly)
     */
    static void bleProcessTxQueue();
};
