#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
//   FIRMWARE & FEATURE FLAGS
// ============================================================================
#define FIRMWARE_VERSION "v1.4.1"
#define FIRMWARE_NAME "LoRaLink Any2Any"
#define HARDWARE_ID "Heltec ESP32 LoRa V3"
#define CONFIG_SCHEMA "1.0"
#define ALLOW_GPIO_CONTROL true

// ============================================================================
//   LoRa Radio Settings
// ============================================================================
#define LORA_FREQ 915.0
#define LORA_BW 250.0
#define LORA_SF 10
#define LORA_CR 5
#define LORA_SYNC 0x12 // Private Network
#define LORA_PWR 10    // dBm

// ============================================================================
//   ESP-NOW Settings
// ============================================================================
#define ESPNOW_CHANNEL 1
#define ESPNOW_MAX_PEERS 10
#define ESPNOW_QUEUE_SIZE 8

// ============================================================================
//   GPIO PIN MAPPING (Heltec WiFi LoRa 32 V3)
// ============================================================================
#define PIN_LED_BUILTIN 35 // Orange LED
#define PIN_BUTTON_PRG 0   // PRG Button
#define PIN_BAT_ADC 1      // Battery ADC
#define PIN_VEXT_CTRL 36   // External Power (LOW = ON for Heltec V3)
#define PIN_LORA_CS 8      // LoRa Chip Select
#define PIN_LORA_DIO1 14   // LoRa IRQ
#define PIN_LORA_RST 12    // LoRa Reset
#define PIN_LORA_BUSY 13   // LoRa Busy
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_OLED_RST 21

// GPS Placeholder Pins
#define PIN_GPS_RX 47
#define PIN_GPS_TX 48

// Relay & Sensor Pins
#define PIN_RELAY_110V 5
#define PIN_RELAY_12V_1 46
#define PIN_RELAY_12V_2 6
#define PIN_RELAY_12V_3 7
#define PIN_SENSOR_DHT 15

// ============================================================================
//   COMMUNICATION INTERFACE ENUM
//   Note: Values prefixed with COMM_ to avoid Arduino.h macro conflicts
//   (Arduino #defines SERIAL, WIFI, INPUT etc.)
// ============================================================================
enum class CommInterface : uint8_t {
  COMM_SERIAL = 0,
  COMM_LORA = 1,
  COMM_BLE = 2,
  COMM_WIFI = 3,
  COMM_ESPNOW = 4,
  COMM_INTERNAL = 5
};

// ============================================================================
//   DATA STRUCTURES
// ============================================================================

// Binary Telemetry Struct removed in favor of JSON string payloads
// Data Packet Structure (Optimized: 64 bytes total)
struct __attribute__((packed)) MessagePacket {
  char sender[16];   // Readable Sender Name
  char text[45];     // Message Text
  uint8_t ttl;       // Time-To-Live
  uint16_t checksum; // Integrity check to filter noise
};

#define MAX_TTL 3

// Encrypted packet buffer size (12 IV + 16 Tag + 64 ciphertext)
#define ENCRYPTED_PACKET_SIZE 92

// Remote Node Tracking
struct RemoteNode {
  char id[16];
  uint32_t lastSeen;
  float battery;
  uint8_t resetCode;
  uint32_t uptime;
  int16_t rssi;
  uint8_t hops; // 0 = direct neighbor, 1+ = relayed
  float lat;
  float lon;
};

// ESP-NOW Peer Info
struct ESPNowPeer {
  uint8_t mac[6];
  char name[16];
  bool active;
};

#define MAX_NODES 20
#define LOG_SIZE 20
#define HASH_BUFFER_SIZE 20

#endif // CONFIG_H
