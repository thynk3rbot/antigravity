"""
Must match firmware/magic/lib/Mx/mx_subjects.h EXACTLY
"""

SUBJECTS = {
    0x0001: "node_status",
    0x0002: "relay_state",
    0x0003: "sensor_data",
    0x0004: "gps_position",
    0x0010: "command",
    0x0011: "command_reply",
    0x0020: "mesh_neighbor",
    0x0021: "mesh_route",
    0x0030: "ota_announce",
    0x0040: "schedule",
    0x00FF: "heartbeat",
}

BY_NAME = {v: k for k, v in SUBJECTS.items()}
