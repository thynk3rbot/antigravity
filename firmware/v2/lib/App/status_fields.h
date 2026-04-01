/**
 * @file status_fields.h
 * @brief Central field inclusion list for STATUS and VSTATUS responses.
 *
 * Edit this table to control exactly which JSON fields appear in each response.
 *
 * Columns:
 *   name       — JSON key (must match what StatusBuilder writes)
 *   page       — OLED page where this field is displayed (0 = not on screen)
 *   inStatus   — include in STATUS (lightweight, over-the-air friendly)
 *   inVStatus  — include in VSTATUS (verbose, for web/BLE/USB)
 *   enabled    — master switch; false removes the field from both responses
 */

#pragma once

struct StatusFieldDef {
    const char* name;
    uint8_t     page;
    bool        inStatus;
    bool        inVStatus;
    bool        enabled;
};

// ─────────────────────────────────────────────────────────────────────────────
// EDIT THIS TABLE to add / remove / toggle fields in STATUS and VSTATUS.
// Order here does not affect JSON output order.
// ─────────────────────────────────────────────────────────────────────────────
static const StatusFieldDef STATUS_FIELD_TABLE[] = {

    // ── Identity ─────────────────────────────────────── page  S      VS
    { "name",               1,   true,   true,   true  },
    { "ver",                0,   true,   true,   true  },
    { "hw",                 0,   false,  true,   true  },
    { "id",                 0,   false,  true,   true  },
    { "hw_id",              0,   false,  true,   true  },
    { "mac",                1,   false,  true,   true  },
    { "ip",                 1,   true,   true,   true  },

    // ── Power / Battery ──────────────────────────────── page  S      VS
    { "bat_pct",            2,   true,   true,   true  },
    { "bat_percentage",     2,   false,  true,   true  },
    { "bat_v",              2,   false,  true,   true  },
    { "bat",                2,   false,  true,   true  },
    { "mode",               2,   true,   true,   true  },
    { "vext",               2,   false,  true,   true  },

    // ── LoRa ─────────────────────────────────────────── page  S      VS
    { "lora_rssi",          2,   true,   true,   true  },
    { "lora_snr",           2,   false,  true,   true  },
    { "rssi_history",       2,   false,  true,   true  },

    // ── WiFi / BLE / MQTT ────────────────────────────── page  S      VS
    { "wifi_connected",     3,   true,   true,   true  },
    { "wifi_rssi",          3,   false,  true,   true  },
    { "ble_connected",      3,   true,   true,   true  },
    { "ble_enabled",        3,   false,  true,   true  },
    { "ble_device_name",    3,   false,  true,   true  },
    { "mqtt_connected",     3,   true,   true,   true  },
    { "mqtt_broker",        3,   false,  true,   true  },
    { "transports",         3,   false,  true,   true  },

    // ── Mesh / Peers ─────────────────────────────────── page  S      VS
    { "peer_cnt",           1,   true,   true,   true  },
    { "peers",              1,   false,  true,   true  },

    // ── Relay ────────────────────────────────────────── page  S      VS
    { "relay",              4,   true,   true,   true  },

    // ── Telemetry (temperature etc.) ─────────────────── page  S      VS
    { "temp_c",             4,   true,   true,   true  },
    { "telemetry",          4,   false,  true,   false }, // placeholder data — off

    // ── GPS ──────────────────────────────────────────── page  S      VS
    { "gps",                5,   true,   true,   true  },

    // ── System ───────────────────────────────────────── page  S      VS
    { "uptime",             4,   true,   true,   true  },
    { "uptime_seconds",     4,   false,  true,   true  },
    { "heap",               4,   false,  true,   true  },
    { "heap_percentage",    4,   false,  true,   true  },
    { "boot_count",         0,   false,  true,   false }, // placeholder — off
    { "last_status_update", 0,   false,  false,  false }, // internal — off
    { "last_command",       0,   false,  false,  false }, // internal — off
    { "command_queue_length",0,  false,  false,  false }, // internal — off
    { "crypto_enabled",     0,   false,  true,   true  },
    { "friendly_name",      0,   false,  false,  false }, // duplicate of name — off
    { "location",           0,   false,  true,   true  },
    { "schedule",           0,   false,  true,   false }, // not wired — off
    { "active_product",     0,   false,  true,   true  },
    { "plugins",            0,   false,  true,   true  },

    // ── Noise / background data ──────────────────────── page  S      VS
    { "detected_devices",   0,   false,  false,  false }, // marauder — off by default
};

static constexpr int STATUS_FIELD_COUNT =
    (int)(sizeof(STATUS_FIELD_TABLE) / sizeof(STATUS_FIELD_TABLE[0]));
