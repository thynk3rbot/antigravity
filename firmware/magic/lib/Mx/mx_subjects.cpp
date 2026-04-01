#include "mx_subjects.h"

const char* MxSubjects::nameOf(uint16_t id) {
    switch (id) {
        case NODE_STATUS:    return "NODE_STATUS";
        case RELAY_STATE:    return "RELAY_STATE";
        case SENSOR_DATA:    return "SENSOR_DATA";
        case GPS_POSITION:   return "GPS_POSITION";
        case COMMAND:        return "COMMAND";
        case COMMAND_REPLY:  return "COMMAND_REPLY";
        case MESH_NEIGHBOR:  return "MESH_NEIGHBOR";
        case MESH_ROUTE:     return "MESH_ROUTE";
        case OTA_ANNOUNCE:   return "OTA_ANNOUNCE";
        case SCHEDULE:       return "SCHEDULE";
        case HEARTBEAT:      return "HEARTBEAT";
        default:             return "UNKNOWN_SUBJECT";
    }
}
