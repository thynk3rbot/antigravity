#pragma once
#include <cstdint>

/**
 * MxSubjects — Registry of subject IDs. Both firmware and daemon must agree on these.
 */
namespace MxSubjects {
    constexpr uint16_t NODE_STATUS    = 0x0001;
    constexpr uint16_t RELAY_STATE    = 0x0002;
    constexpr uint16_t SENSOR_DATA    = 0x0003;
    constexpr uint16_t GPS_POSITION   = 0x0004;
    constexpr uint16_t COMMAND        = 0x0010;
    constexpr uint16_t COMMAND_REPLY  = 0x0011;
    constexpr uint16_t MESH_NEIGHBOR  = 0x0020;
    constexpr uint16_t MESH_ROUTE     = 0x0021;
    constexpr uint16_t OTA_ANNOUNCE   = 0x0030;
    constexpr uint16_t SCHEDULE       = 0x0040;
    constexpr uint16_t HEARTBEAT      = 0x00FF;

    const char* nameOf(uint16_t id);
}
