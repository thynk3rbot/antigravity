#pragma once
#include <cstdint>
#include <cstring>

/**
 * MxRecord — Holds current state per subject.
 * The bandwidth strategy for mesh — only deltas travel over the wire.
 */
struct MxRecord {
    uint16_t subject_id;
    uint32_t sequence;          // monotonic — receivers detect gaps
    uint32_t timestamp_ms;      // millis() at last update
    uint8_t dirty_mask;         // bitmask of changed fields since last publish
};

/**
 * Example MxNodeStatus record provided by framework docs.
 */
struct MxNodeStatus : public MxRecord {
    uint16_t battery_mv;        // field 0
    int8_t rssi;                // field 1
    float temperature;          // field 2
    uint8_t relay_mask;         // field 3
    uint32_t uptime_s;          // field 4
    float latitude;             // field 5
    float longitude;            // field 6
    float altitude;             // field 7
};
