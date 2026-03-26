/**
 * @file control_packet.h
 * @brief LoRaLink v2 Control Packet Definition (Packed C-struct)
 *
 * Defines the binary protocol for all mesh communications.
 * No Protocol Buffers - direct memory mapping for efficiency.
 * All packets fit within a single LoRa frame (~240 bytes max).
 */

#pragma once

#include <cstdint>
#include <cstring>

// ============================================================================
// Packet Type Enumeration
// ============================================================================

enum class PacketType : uint8_t {
  TELEMETRY = 0x01,    // Node -> Hub (or Hub -> node, periodic)
  ACTION = 0x02,        // Hub -> Node or Node -> Node (relay toggle)
  ACK = 0x03,           // Any -> Any (acknowledgment for reliability)
  MESH_PROBE = 0x04,    // Discovery/beacon packet
  HEARTBEAT = 0x05,     // Periodic life-sign
  GPS_LOCATION = 0x06,  // Node -> Any (GPS coordinates)
  GPIO_SET = 0x07,      // Mesh-enabled GPIO control
  RESERVED_08 = 0x08,
  RESERVED_09 = 0x09,
  RESERVED_0A = 0x0A,
  UNKNOWN = 0xFF
};

// ============================================================================
// Packet Flags (16-bit field in header)
// ============================================================================

#define PKT_FLAG_REQUIRE_ACK    (1 << 0)   // Sender wants ACK
#define PKT_FLAG_IS_RELAY       (1 << 1)   // Packet is relayed (not direct)
#define PKT_FLAG_ENCRYPTED      (1 << 2)   // Payload is encrypted (always true in v2)
#define PKT_FLAG_RESERVED_3     (1 << 3)
#define PKT_FLAG_RESERVED_4     (1 << 4)
#define PKT_FLAG_RESERVED_5     (1 << 5)
// ... bits 6-15 reserved

// ============================================================================
// Packed C-Struct: Packet Header (6 bytes)
// ============================================================================

#pragma pack(1)  // Force byte alignment, no padding

/**
 * @struct PacketHeader
 * @brief LoRa packet header (6 bytes)
 *
 * Layout:
 *   Byte 0: Packet Type (PacketType enum)
 *   Byte 1: Source Node ID (0-255; 0 = Hub)
 *   Byte 2: Dest Node ID (0-255; 255 = broadcast)
 *   Byte 3: Sequence Number (for dedup, retries)
 *   Bytes 4-5: Flags (16-bit, see PKT_FLAG_* above)
 */
struct PacketHeader {
  uint8_t type;      // PacketType
  uint8_t src;       // Source node ID (0 = Hub)
  uint8_t dest;      // Dest node ID (255 = broadcast)
  uint8_t seq;       // Sequence number for this src->dest pair
  uint16_t flags;    // Packed flags (little-endian)

  // Helper: check if ACK requested
  bool requiresACK() const { return flags & PKT_FLAG_REQUIRE_ACK; }

  // Helper: check if relayed
  bool isRelayed() const { return flags & PKT_FLAG_IS_RELAY; }

  // Helper: is broadcast
  bool isBroadcast() const { return dest == 255; }
};

static_assert(sizeof(PacketHeader) == 6, "PacketHeader must be 6 bytes");

// ============================================================================
// Payload Union: Telemetry (8 bytes)
// ============================================================================

/**
 * @struct TelemetryPayload
 * @brief Sensor telemetry reported by nodes (8 bytes)
 *
 * Reported periodically or on-demand by nodes to hub.
 * Packed with scaling factors to fit in minimal bytes.
 */
struct TelemetryPayload {
  uint16_t tempC_x10;      // Temperature in 0.1°C units (0.0 - 6553.5°C)
  uint16_t voltageV_x100;  // Supply voltage in 0.01V units (0.00V - 655.35V)
  uint8_t relayState;      // Bitmask of relay states (8 channels)
  uint8_t rssi;            // RSSI magnitude (0-255, maps to dBm)
  uint16_t uptime_min;     // Uptime in minutes (0 - 65535 min ≈ 45 days)
};

static_assert(sizeof(TelemetryPayload) == 8, "TelemetryPayload must be 8 bytes");

// ============================================================================
// Payload Union: Action (2 bytes)
// ============================================================================

/**
 * @struct ActionPayload
 * @brief Relay control command (2 bytes)
 *
 * Sent by hub (or relayed by nodes) to toggle/set relays.
 * Uses atomic bitmask operations for multiple relay changes.
 */
struct ActionPayload {
  uint8_t relayMask;    // Which relays to affect (bitmask, 1 = toggle, 0 = no-op)
  uint8_t relayState;   // Desired state for toggled relays (1 = ON, 0 = OFF)
};

static_assert(sizeof(ActionPayload) == 2, "ActionPayload must be 2 bytes");

// ============================================================================
// Payload Union: GPS (8 bytes)
// ============================================================================

/**
 * @struct GpsPayload
 * @brief GPS coordinates reported by nodes (8 bytes)
 *
 * Reported periodically or on-demand by nodes.
 * Uses 1e7 scaling for 32-bit integer storage (approx 0.11m precision).
 */
struct GpsPayload {
  int32_t lat_x1e7;        // Latitude * 1,000,000,0 (range: -90e7 to 90e7)
  int32_t lon_x1e7;        // Longitude * 1,000,000,0 (range: -180e7 to 180e7)
};

static_assert(sizeof(GpsPayload) == 8, "GpsPayload must be 8 bytes");

// ============================================================================
// Payload Union: GPIO (4 bytes)
// ============================================================================

/**
 * @struct GpioPayload
 * @brief Remote GPIO control (byte-aligned)
 */
struct GpioPayload {
  uint8_t pin;          // Pin number (0-47)
  uint8_t action;       // 0=OFF, 1=ON, 2=TOGGLE
  uint16_t duration_ms; // Pulse duration (0 = persistent)
  uint16_t command_id;  // For tracking/ACK alignment
  uint16_t reserved;    // Padding to 8 bytes for union alignment
};

static_assert(sizeof(GpioPayload) == 8, "GpioPayload must be 8 bytes");

// ============================================================================
// Complete Control Packet (14 bytes)
// ============================================================================

/**
 * @struct ControlPacket
 * @brief Complete LoRaLink v2 packet (14 bytes)
 *
 * Memory layout:
 *   Bytes 0-5:   PacketHeader (6 bytes)
 *   Bytes 6-13:  Payload union (8 bytes max)
 *   Total: 14 bytes (fits in LoRa MTU)
 *
 * Usage:
 *   - Telemetry: header.type = TELEMETRY, populate payload.telemetry
 *   - Action: header.type = ACTION, populate payload.action
 *   - ACK: header.type = ACK, no payload needed
 */
struct ControlPacket {
  PacketHeader header;

  union {
    TelemetryPayload telemetry;
    ActionPayload action;
    GpsPayload gps;
    GpioPayload gpio;
    uint8_t raw[8];  // Raw byte access (8 bytes)
  } payload;

  // ========================================================================
  // Static Factory Methods
  // ========================================================================

  /**
   * @brief Create a telemetry packet
   * @param src Source node ID
   * @param dest Destination (0xFF = broadcast to Hub)
   * @param tempC_x10 Temperature in 0.1°C units
   * @param voltageV_x100 Voltage in 0.01V units
   * @param relayState 8-bit relay bitmask
   * @param rssi RSSI magnitude (0-255)
   * @return Initialized ControlPacket
   */
  static ControlPacket makeTelemetry(
    uint8_t src, uint8_t dest,
    uint16_t tempC_x10, uint16_t voltageV_x100,
    uint8_t relayState, uint8_t rssi
  ) {
    ControlPacket pkt;
    pkt.header.type = static_cast<uint8_t>(PacketType::TELEMETRY);
    pkt.header.src = src;
    pkt.header.dest = dest;
    pkt.header.seq = 0;  // Caller should increment
    pkt.header.flags = 0;

    pkt.payload.telemetry.tempC_x10 = tempC_x10;
    pkt.payload.telemetry.voltageV_x100 = voltageV_x100;
    pkt.payload.telemetry.relayState = relayState;
    pkt.payload.telemetry.rssi = rssi;
    pkt.payload.telemetry.uptime_min = 0;

    return pkt;
  }

  /**
   * @brief Create an action packet
   * @param src Source node ID
   * @param dest Destination node ID
   * @param toggleMask Which relays to toggle (1 = toggle, 0 = no-op)
   * @param desiredState Desired state for toggled relays (1 = ON, 0 = OFF)
   * @return Initialized ControlPacket
   */
  static ControlPacket makeAction(
    uint8_t src, uint8_t dest,
    uint8_t toggleMask, uint8_t desiredState
  ) {
    ControlPacket pkt;
    pkt.header.type = static_cast<uint8_t>(PacketType::ACTION);
    pkt.header.src = src;
    pkt.header.dest = dest;
    pkt.header.seq = 0;  // Caller should increment
    pkt.header.flags = PKT_FLAG_REQUIRE_ACK;

    pkt.payload.action.relayMask = toggleMask;
    pkt.payload.action.relayState = desiredState;

    return pkt;
  }

  /**
   * @brief Create an acknowledgment packet
   * @param src Source node ID
   * @param dest Destination node ID
   * @param seqNum Sequence number being acked
   * @return Initialized ControlPacket
   */
  static ControlPacket makeACK(uint8_t src, uint8_t dest, uint8_t seqNum) {
    ControlPacket pkt;
    pkt.header.type = static_cast<uint8_t>(PacketType::ACK);
    pkt.header.src = src;
    pkt.header.dest = dest;
    pkt.header.seq = seqNum;
    pkt.header.flags = 0;

    memset(pkt.payload.raw, 0, sizeof(pkt.payload.raw));

    return pkt;
  }

  /**
   * @brief Create a GPS coordinate packet
   * @param src Source node ID
   * @param dest Destination (0xFF = broadcast)
   * @param lat Latitude (double)
   * @param lon Longitude (double)
   * @param requireAck If true, requests ACK from destination
   * @return Initialized ControlPacket
   */
  static ControlPacket makeGPS(uint8_t src, uint8_t dest, double lat, double lon, bool requireAck = false) {
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.type = static_cast<uint8_t>(PacketType::GPS_LOCATION);
    pkt.header.src = src;
    pkt.header.dest = dest;
    pkt.header.seq = 0;
    pkt.header.flags = 0;
    if (requireAck) pkt.header.flags |= PKT_FLAG_REQUIRE_ACK;

    pkt.payload.gps.lat_x1e7 = static_cast<int32_t>(lat * 10000000.0);
    pkt.payload.gps.lon_x1e7 = static_cast<int32_t>(lon * 10000000.0);
    return pkt;
  }

  /**
   * @brief Create a heartbeat/beacon packet
   * @param src Source node ID
   * @return Initialized ControlPacket
   */
  static ControlPacket makeHeartbeat(uint8_t src) {
    ControlPacket pkt;
    pkt.header.type = static_cast<uint8_t>(PacketType::HEARTBEAT);
    pkt.header.src = src;
    pkt.header.dest = 0xFF;  // Broadcast
    pkt.header.seq = 0;
    pkt.header.flags = 0;

    memset(pkt.payload.raw, 0, sizeof(pkt.payload.raw));

    return pkt;
  }

  /**
   * @brief Create a GPIO control packet
   * @param src Source node ID
   * @param dest Destination node ID
   * @param pin Pin number
   * @param action 0=OFF, 1=ON, 2=TOGGLE
   * @param duration Pulse duration (0=persistent)
   * @return Initialized ControlPacket
   */
  static ControlPacket makeGpioSet(uint8_t src, uint8_t dest, uint8_t pin, uint8_t action, uint16_t duration = 0) {
    ControlPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.type = static_cast<uint8_t>(PacketType::GPIO_SET);
    pkt.header.src = src;
    pkt.header.dest = dest;
    pkt.header.seq = 0; // Increment before send
    pkt.header.flags = PKT_FLAG_REQUIRE_ACK;

    pkt.payload.gpio.pin = pin;
    pkt.payload.gpio.action = action;
    pkt.payload.gpio.duration_ms = duration;
    pkt.payload.gpio.command_id = 0; // TBD
    return pkt;
  }
};

static_assert(sizeof(ControlPacket) == 14, "ControlPacket must be 14 bytes");

#pragma pack()  // Reset to default alignment

// ============================================================================
// Compile-Time Validation
// ============================================================================

// Ensure packet fits in typical LoRa MTU (~240 bytes)
static_assert(sizeof(ControlPacket) <= 240, "ControlPacket exceeds LoRa MTU");

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert RSSI magnitude (0-255) to dBm
 * @param rssiByte Magnitude value (0-255)
 * @return RSSI in dBm (negative, e.g., -80)
 */
inline int8_t rssiByteToDbm(uint8_t rssiByte) {
  // Linear mapping: 0 -> 0 dBm, 255 -> -120 dBm
  return static_cast<int8_t>(-120 + (rssiByte / 2));
}

/**
 * @brief Convert RSSI in dBm to magnitude byte
 * @param rssiDbm RSSI in dBm (negative)
 * @return Magnitude (0-255)
 */
inline uint8_t rssiDbmToByte(int8_t rssiDbm) {
  // Reverse mapping
  int16_t val = (rssiDbm + 120) * 2;
  if (val < 0) val = 0;
  if (val > 255) val = 255;
  return static_cast<uint8_t>(val);
}
