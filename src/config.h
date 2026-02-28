#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
//   FIRMWARE & FEATURE FLAGS
// ============================================================================
#define FIRMWARE_VERSION "v1.0"
#define FIRMWARE_NAME    "LoRaLink-AnyToAny"
#define ALLOW_GPIO_CONTROL true

// ============================================================================
//   LoRa Radio Settings
// ============================================================================
#define LORA_FREQ 915.0
#define LORA_BW   250.0
#define LORA_SF   10
#define LORA_CR   5
#define LORA_SYNC 0x12  // Private Network
#define LORA_PWR  10    // dBm

// ============================================================================
//   ESP-NOW Settings
// ============================================================================
#define ESPNOW_CHANNEL       1
#define ESPNOW_MAX_PEERS     10
#define ESPNOW_QUEUE_SIZE    8

// ============================================================================
//   GPIO PIN MAPPING (Heltec WiFi LoRa 32 V3)
// ============================================================================
#define PIN_LED_BUILTIN 35  // Orange LED
#define PIN_BUTTON_PRG  0   // PRG Button
#define PIN_BAT_ADC     1   // Battery ADC
#define PIN_VEXT_CTRL   36  // External Power (LOW = ON for Heltec V3)
#define PIN_LORA_CS     8   // LoRa Chip Select
#define PIN_LORA_DIO1   14  // LoRa IRQ
#define PIN_LORA_RST    12  // LoRa Reset
#define PIN_LORA_BUSY   13  // LoRa Busy
#define PIN_OLED_SDA    17
#define PIN_OLED_SCL    18
#define PIN_OLED_RST    21

// GPS Placeholder Pins
#define PIN_GPS_RX 47
#define PIN_GPS_TX 48

// Relay & Sensor Pins
#define PIN_RELAY_110V  5
#define PIN_RELAY_12V_1 14
#define PIN_RELAY_12V_2 6
#define PIN_RELAY_12V_3 7
#define PIN_SENSOR_DHT  15

// ============================================================================
//   COMMUNICATION INTERFACE ENUM
// ============================================================================
enum class CommInterface : uint8_t {
  SERIAL = 0,
  LORA   = 1,
  BLE    = 2,
  WIFI   = 3,
  ESPNOW = 4,
  INTERNAL = 5
};

// ============================================================================
//   DATA STRUCTURES
// ============================================================================

// Binary Telemetry Struct
struct TelemetryPacket {
  uint32_t uptime;    // Seconds
  float    battery;   // Voltage
  int16_t  rssi;      // Gateway RSSI
  uint8_t  resetCode; // Why we rebooted last
  float    lat;       // Placeholder - Latitude
  float    lon;       // Placeholder - Longitude
  char     padding[13]; // Align to block size
};

// Data Packet Structure (Optimized: 64 bytes total)
struct MessagePacket {
  char     sender[16];   // Readable Sender Name
  char     text[46];     // Message Text
  uint16_t checksum;     // Integrity check to filter noise
};

// Encrypted packet buffer size (16 IV + 64 ciphertext)
#define ENCRYPTED_PACKET_SIZE 80

// Remote Node Tracking
struct RemoteNode {
  char     id[16];
  uint32_t lastSeen;
  float    battery;
  uint8_t  resetCode;
  uint32_t uptime;
  int16_t  rssi;
  float    lat;
  float    lon;
};

// ESP-NOW Peer Info
struct ESPNowPeer {
  uint8_t mac[6];
  char    name[16];
  bool    active;
};

#define MAX_NODES        20
#define LOG_SIZE         20
#define HASH_BUFFER_SIZE 20

#endif // CONFIG_H
