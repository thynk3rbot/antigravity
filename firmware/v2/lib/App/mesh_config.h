/**
 * @file mesh_config.h
 * @brief Mesh Protocol Configuration (V1 Parity)
 */

#pragma once

#include <stdint.h>

// Discovery timing
#define DISCOVERY_BURST_MS    300000UL  // 5 minutes of fast heartbeats on boot
#define DISCOVERY_INTERVAL_S  20UL      // 20s interval during discovery seeking
#define USB_HEART_BEAT_S      60UL      // 60s interval when on USB power
#define NORMAL_HEART_BEAT_S   300UL     // 5 minutes (standard)

// V1 Binary Protocol
#define V1_BINARY_TOKEN       0xAA

enum class V1BinaryCmd : uint8_t {
    BC_NOP = 0x00,
    BC_GPIO_SET = 0x01,
    BC_PWM_SET = 0x02,
    BC_SERVO_SET = 0x03,
    BC_READ_PIN = 0x04,
    BC_REBOOT = 0x05,
    BC_PING = 0x06,
    BC_STATUS = 0x07,
    BC_ACK = 0x08,
    BC_CONFIG_SEG = 0x09,
    BC_HEARTBEAT = 0x0A
};
