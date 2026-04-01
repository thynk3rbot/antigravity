// firmware/magic/lib/Mx/mx_record.h
#pragma once
#include <cstdint>
#include <cstring>

/**
 * MxRecord — Last Value Cache (LVC) record base.
 * Sized for fixed subjects.
 */
struct MxRecord {
    uint16_t subject_id;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint8_t dirty_mask; // 8 bits for 8 fields max per standard record
    
    void clear_dirty() { dirty_mask = 0; }
    bool is_dirty() const { return dirty_mask != 0; }
    void mark_dirty(uint8_t field_idx) { dirty_mask |= (1 << field_idx); }
};

/**
 * Example: Node Status Record
 * Standard fields for the Magic platform.
 */
struct MxNodeStatus : public MxRecord {
    uint16_t battery_mv; // field 0
    int8_t rssi;         // field 1
    float temperature;   // field 2
    uint8_t relay_mask;  // field 3
    uint32_t uptime_s;   // field 4
    float latitude;      // field 5
    float longitude;     // field 6
    float altitude;      // field 7
};

/**
 * GpsPosition Record
 * Dedicated for high-precision tracking nodes (V4, T-Beam).
 */
struct MxGpsPosition : public MxRecord {
    float latitude;      // field 0
    float longitude;     // field 1
    float altitude;      // field 2
    uint8_t num_sats;    // field 3
    uint16_t fix_age_ms; // field 4
    uint16_t hdop_scaled; // field 5 (hdop * 100)
};
