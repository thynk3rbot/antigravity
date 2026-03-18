#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
//   FIRMWARE & FEATURE FLAGS
// ============================================================================
#define FIRMWARE_VERSION "v0.2.4"
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
#define LORA_PWR 14    // dBm - Standard Power (Battery Connected)

// ── LoRa CAD (Wake-on-Radio) ────────────────────────────────────────────────
#define LORA_CAD_ON true               // global capability toggle
#define LORA_CAD_INTERVAL_MS 1000      // duty cycle check interval
#define LORA_PREAMBLE_SHORT 8          // standard preamble
#define LORA_PREAMBLE_LONG 512         // extended preamble for wake-up

// ============================================================================
//   ESP-NOW Settings
// ============================================================================
#define ESPNOW_CHANNEL 1
#define ESPNOW_MAX_PEERS 10
#define ESPNOW_QUEUE_SIZE 8

// ============================================================================
//   GPIO PIN MAPPING
// ============================================================================

#ifdef ARDUINO_LORA_HELTEC_V2
// ── Heltec WiFi LoRa 32 V2 (ESP32) ──────────────────────────────────────────
#define PIN_LED_BUILTIN 25
#define PIN_BUTTON_PRG 0
#define PIN_BAT_ADC 34
#define PIN_VEXT_CTRL 21 // External Power (HIGH = OFF for V2)
#define PIN_LORA_CS 18
#define PIN_LORA_DIO1 26
#define PIN_LORA_RST 14
#define PIN_LORA_BUSY -1 // No busy pin on V2
#define PIN_OLED_SDA 4
#define PIN_OLED_SCL 15
#define PIN_OLED_RST 16
#define PIN_BAT_CTRL -1
#define BAT_VOLT_MULTI 3.20f // Standard divider on V2
#define PIN_GPS_RX 12
#define PIN_GPS_TX 13
#define PIN_RELAY_110V 17
#define PIN_RELAY_12V_1 22
#define PIN_RELAY_12V_2 23
#define PIN_RELAY_12V_3 27
#define PIN_SENSOR_DHT 2
#define PIN_MCP_INT 13
#elif defined(ARDUINO_LORA_HELTEC_V4)
// ── Heltec WiFi LoRa 32 V4 (ESP32-S3) ───────────────────────────────────────
#define PIN_LED_BUILTIN 35
#define PIN_BUTTON_PRG 0
#define PIN_BAT_ADC 1
#define PIN_VEXT_CTRL 36
#define PIN_LORA_CS 8
#define PIN_LORA_DIO1 14
#define PIN_LORA_RST 12
#define PIN_LORA_BUSY 13
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_OLED_RST 21
#define PIN_BAT_CTRL 37
#define BAT_VOLT_MULTI 6.600f

// GNSS (GPS) pins for V4
#define PIN_GPS_RX 38
#define PIN_GPS_TX 39
#define PIN_GPS_PPS 41
#define PIN_GPS_RST 42
#define PIN_GPS_WAKE 40

// Relay & Sensor Pins (Shared carrier board layout)
#define PIN_RELAY_110V 5
#define PIN_RELAY_12V_1 46
#define PIN_RELAY_12V_2 6
#define PIN_RELAY_12V_3 7
#define PIN_SENSOR_DHT 15
#define PIN_MCP_INT 45 // V4 header J3 pin 6 is GPIO45 (V3 was 38)
#else
// ── Heltec WiFi LoRa 32 V3 (ESP32-S3) ───────────────────────────────────────
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
#define PIN_BAT_CTRL 37 // Battery Divider Control (LOW = ON)
#define BAT_VOLT_MULTI                                                         \
  6.600f // Heltec V3 (High-Resistor Variant): (560k + 100k) / 100k = 6.6

// GPS Placeholder Pins
#define PIN_GPS_RX 47
#define PIN_GPS_TX 48

// Relay & Sensor Pins
#define PIN_RELAY_110V 5
#define PIN_RELAY_12V_1 46
#define PIN_RELAY_12V_2 6
#define PIN_RELAY_12V_3 7
#define PIN_SENSOR_DHT 15
#define PIN_MCP_INT 38 // INTA → GPIO 38
#endif

// ── MCP23017 I2C GPIO Expander ─────────────────────────────────────────────
// Up to 8 MCP23017 chips share the OLED I2C bus.
#define MCP_PIN_BASE 100        // Native pins 0–99; MCP pins 100–227
#define MCP_CHIP_PINS 16        // 16 GPIO per chip (GPA0–GPB7)
#define MCP_MAX_CHIPS 8         // 8 addresses: 0x20–0x27
#define MCP_CHIP_ADDR_BASE 0x20 // I2C base address (A0=A1=A2=GND)

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
//   TRANSPORT LINK PREFERENCE
//   Which protocol the device targets after boot negotiation.
//   Persisted in NVS (key: "link_pref"). LINK_AUTO = negotiate each boot.
//   Hook: extend with LINK_LORA_BLE_BRIDGE for gateway/relay role.
// ============================================================================
enum class LinkPreference : uint8_t {
  LINK_AUTO = 0,      // Negotiate on boot (factory default)
  LINK_BLE = 1,       // BLE terminal only — WiFi off after lock-in
  LINK_WIFI_MQTT = 2, // WiFi + MQTT bidirectional
  LINK_WIFI_HTTP = 3, // WiFi + HTTP (no broker required)
  LINK_LORA = 4,      // LoRa mesh only — lowest power hold
};

// Result of a WiFi probe attempt
enum class ProbeResult : uint8_t {
  PROBE_OK_MQTT = 0,   // WiFi associated + MQTT broker reached
  PROBE_OK_HTTP = 1,   // WiFi associated, no MQTT
  PROBE_NO_AP = 2,     // SSID not found / association failed
  PROBE_NO_BROKER = 3, // WiFi up but MQTT broker unreachable
  PROBE_TIMEOUT = 4,   // Association timed out
};

// Boot negotiation window — try all configured transports before locking in.
// Override via NVS key "trans_neg_ms". Factory default: 10 000ms
// (configurable).
#define TRANSPORT_NEGOTIATE_MS 10000UL

// WiFi probe backoff — applied when downgraded to LINK_LORA.
// Sequence doubles each failure: 30s → 60s → 2m → ... → 30m cap.
#define PROBE_BACKOFF_MIN_MS 30000UL   //  30 seconds
#define PROBE_BACKOFF_MAX_MS 1800000UL //  30 minutes
#define PROBE_TIMEOUT_MS 5000UL        //   5 seconds max per probe

// ============================================================================
//   DATA STRUCTURES
// ============================================================================

// Binary Telemetry Struct removed in favor of JSON string payloads
// Data Packet Structure (Optimized: 64 bytes total)
struct __attribute__((packed)) MessagePacket {
  char sender[16];   // Readable Sender Name (null-terminated if short)
  char text[45];     // Message Text or Binary Payload
  uint8_t ttl;       // Time-To-Live
  uint16_t checksum; // Integrity check to filter noise
};

// ── Binary Command Protocol (40% Range Boost Goal) ──────────────────────────
// Binary packets start with byte 0xAA in text[0].
// Structure: [0xAA] [TargetID_Short] [CmdCode] [Args...]
#define BINARY_TOKEN 0xAA

enum class BinaryCmd : uint8_t {
  BC_NOP = 0x00,
  BC_GPIO_SET = 0x01,  // [Pin] [0|1]
  BC_PWM_SET = 0x02,   // [Pin] [Duty 0-255]
  BC_SERVO_SET = 0x03, // [Pin] [Angle 0-180]
  BC_READ_PIN = 0x04,  // [Pin]
  BC_REBOOT = 0x05,
  BC_PING = 0x06,
  BC_STATUS = 0x07,
  BC_ACK = 0x08,       // ACK for binary command [AckToken]
  BC_CONFIG_SEG = 0x09, // [SegIndex] [TotalSegs] [Data...]
  BC_HEARTBEAT = 0x0A  // Packaged telemetry: [Uptime_4][Bat_2][RSSI_1][Hops_1][Reset_1][Flags_1]
};

// ============================================================================
//   NON-BLOCKING TIMING CONSTANTS
// ============================================================================
#define LORA_TX_TIMEOUT_MS 5000      // TX watchdog — force RX if stuck
#define REPEATER_JITTER_MIN_MS 150   // Repeater propagation jitter floor
#define REPEATER_JITTER_MAX_MS 500   // Repeater propagation jitter ceiling
#define BEACON_LEGACY_DELAY_MS 500   // Gap between encrypted & legacy beacon
#define SLEEP_PC_GUARD_MS 3000       // Sleep PC-attached guard window
#define SLEEP_COUNTDOWN_STEP_MS 1200 // Sleep countdown step interval
#define ESPNOW_TX_QUEUE_SIZE 12      // ESP-NOW async send queue depth

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
  uint8_t shortId; // Last byte of MAC for binary routing
  char ip[16];     // mDNS-resolved IP (e.g. "172.16.0.26")
  bool online;     // Runtime state — true if seen within timeout
};

// ESP-NOW Peer Info
struct ESPNowPeer {
  uint8_t mac[6];
  char name[16];
  bool active;
};

#define MAX_NODES 64
#define MAX_PERIPHERALS 8
#define LOG_SIZE 20
#define HASH_BUFFER_SIZE 20

// ============================================================================
//   POWER-MISER (SMART AG) CONFIGURATION
// ============================================================================
#define POWER_MISER_VOLT_NORMAL 3.80f
#define POWER_MISER_VOLT_CONSERVE 3.65f
#define POWER_MISER_VOLT_CRITICAL 3.45f
#define POWER_MISER_HYSTERESIS 0.05f   // 50mV deadband to prevent oscillation

#define POWER_MISER_HB_NORMAL 300UL    // 5 min
#define POWER_MISER_HB_CONSERVE 900UL  // 15 min
#define POWER_MISER_HB_CRITICAL 3600UL // 60 min

#define DISCOVERY_BURST_MS 300000UL    // 5 minutes of fast heartbeats on boot
#define DISCOVERY_INTERVAL_S 20UL      // 20s interval during discovery
#define USB_HEARTBEAT_S     60UL       // 60s interval when on USB power

#define POWER_MISER_TREND_SAMPLES 12   // Ring buffer depth (12 × 30s = 6min)
#define POWER_MISER_SAMPLE_INTERVAL_MS 30000UL // 30s between voltage samples

#endif // CONFIG_H
