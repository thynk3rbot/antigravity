/**
 * @file board_config.h
 * @brief LoRaLink v2 Hardware Abstraction Layer (HAL)
 *
 * Centralizes all GPIO pin assignments and enforces compile-time validation
 * to prevent invalid permutations (e.g., V2-code on V3 hardware).
 *
 * Build flags from platformio.ini determine pin mapping:
 *   -D ROLE_HUB / ROLE_NODE
 *   -D RADIO_SX1276 / RADIO_SX1262
 *   -D ARDUINO_HELTEC_WIFI_LORA_32 / V3 / V4
 */

#pragma once

// ============================================================================
// Build Flag Validation (Compile-Time Checks)
// ============================================================================

#if !defined(ROLE_HUB) && !defined(ROLE_NODE)
  #error "Must define ROLE_HUB or ROLE_NODE via build flag"
#endif

#if defined(ROLE_HUB) && defined(ROLE_NODE)
  #error "Cannot define both ROLE_HUB and ROLE_NODE"
#endif

#if !defined(RADIO_SX1276) && !defined(RADIO_SX1262)
  #error "Must define RADIO_SX1276 or RADIO_SX1262 via build flag"
#endif

// Board-to-radio mapping validation
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
  // Heltec V2: ESP32 + SX1276
  #ifndef RADIO_SX1276
    #error "Heltec V2 (ESP32) requires RADIO_SX1276"
  #endif
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
  // Heltec V3: ESP32-S3 + SX1262
  #ifndef RADIO_SX1262
    #error "Heltec V3 (ESP32-S3) requires RADIO_SX1262"
  #endif
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
  // Heltec V4: ESP32-S3R2 + SX1262 + Native USB + PSRAM
  #ifndef RADIO_SX1262
    #error "Heltec V4 (ESP32-S3R2) requires RADIO_SX1262"
  #endif
  #ifndef BOARD_HAS_PSRAM
    #error "Heltec V4 requires BOARD_HAS_PSRAM flag"
  #endif
#endif

// ============================================================================
// LoRa Radio SPI Interface Pins
// ============================================================================

#ifdef RADIO_SX1276
  // Heltec V2: SX1276 pinout
  #define LORA_MOSI     27
  #define LORA_MISO     19
  #define LORA_SCLK     5
  #define LORA_CS       18
  #define LORA_RESET    14
  #define LORA_DIO0     26
  #define LORA_DIO1     33       // For future FHSS support
  #define LORA_BUSY     -1       // V2 does not have BUSY pin
  #define LORA_FREQ_MHZ 915.0    // ISM band
  #define LORA_BW_KHZ   125.0    // Bandwidth

#elif defined(RADIO_SX1262)
  // Heltec V3/V4: SX1262 pinout
  #define LORA_MOSI     10
  #define LORA_MISO     11
  #define LORA_SCLK     9
  #define LORA_CS       8
  #define LORA_RESET    12
  #define LORA_DIO1     14
  #define LORA_BUSY     13
  #define LORA_FREQ_MHZ 915.0    // ISM band
  #define LORA_BW_KHZ   125.0    // Bandwidth
#endif

// ============================================================================
// I2C Display (SSD1306 OLED, shared across V2/V3/V4)
// ============================================================================

#define I2C_SDA       4
#define I2C_SCL       15
#define I2C_FREQ_HZ   400000
#define OLED_ADDRESS  0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64

// ============================================================================
// Relay GPIO Pins (Generic, supports up to 8 channels via bitmask)
// ============================================================================
// Note: Relay channels are addressed by index (0-7) in ControlPacket.
// The actual GPIO mapping is defined here for hardware control.
// Nodes may not use all 8; this defines the maximum available.

#ifdef RADIO_SX1276
  // V2 Hub: relay pins safe for ESP32 + SX1276 (GPIO 8-14 not used by SX1276)
  #define RELAY_CH0   32
  #define RELAY_CH1   33
  #define RELAY_CH2   25
  #define RELAY_CH3   26
  #define RELAY_CH4   12
  #define RELAY_CH5   13
  #define RELAY_CH6   21
  #define RELAY_CH7   22
#elif defined(RADIO_SX1262)
  // V3/V4 Node: SX1262 occupies GPIO 8,9,10,11,12,13,14
  // GPIO 12 = LORA_RESET, GPIO 13 = LORA_BUSY — CANNOT drive as relay outputs
  // Use 255 as sentinel ("not connected"); relay_hal.cpp skips pins >= 255
  #define RELAY_CH0   255
  #define RELAY_CH1   255
  #define RELAY_CH2   255
  #define RELAY_CH3   255
  #define RELAY_CH4   255
  #define RELAY_CH5   255
  #define RELAY_CH6   255
  #define RELAY_CH7   255
#endif

#define MAX_RELAY_CHANNELS 8

// ============================================================================
// ADC Pins (Telemetry: Temperature, Voltage Monitoring)
// ============================================================================

#define ADC_VBATT     35        // Battery voltage (analog input with divider)
#define ADC_VBATT_DIV 6.6f      // Voltage divider ratio (Heltec standard)
#define ADC_TEMP      36        // ESP32 internal temperature sensor

// ============================================================================
// System GPIO (Control, Status)
// ============================================================================

#define GPIO_PRG_BTN  0         // PRG button for factory reset (active-LOW)
#define GPIO_VEXT     36        // VEXT control (active-LOW, powers display/LoRa)

// ============================================================================
// UART Serial Port (Debug, CLI)
// ============================================================================

#define SERIAL_BAUD   115200
#define SERIAL_RX     3         // RX pin (hardware)
#define SERIAL_TX     1         // TX pin (hardware)

// ============================================================================
// Conditional: Hub-Only Features
// ============================================================================

#ifdef ROLE_HUB
  // Hub has WiFi capability (all Heltec boards support this)
  #define ENABLE_WIFI_TRANSPORT
  #define ENABLE_MQTT_TRANSPORT
  // #define ENABLE_HTTP_OTA      // Optional for v2.1
#else
  // Nodes: explicitly disable WiFi-dependent features
  #undef ENABLE_WIFI_TRANSPORT
  #undef ENABLE_MQTT_TRANSPORT
#endif

// V4-only features (PSRAM available)
#ifdef BOARD_HAS_PSRAM
  #define ENABLE_LARGE_BUFFERS
  #define ENABLE_TELEMETRY_RING   // Optional: circular buffer for telemetry history
#endif

// ============================================================================
// Validation: Prevent Invalid Transport Combinations
// ============================================================================

#ifdef ROLE_NODE
  #if defined(ENABLE_MQTT_TRANSPORT)
    #error "Nodes cannot enable MQTT_TRANSPORT; this is Hub-only"
  #endif
  #if defined(ENABLE_HTTP_OTA)
    #error "Nodes cannot enable HTTP_OTA without WiFi bridging"
  #endif
#endif

// ============================================================================
// System Configuration & Timeouts
// ============================================================================

#define RTOS_TICK_PERIOD_MS     1000
#define RADIO_RX_TIMEOUT_MS     100
#define RADIO_TX_TIMEOUT_MS     5000
#define CONTROL_LOOP_PERIOD_MS  500
#define TELEMETRY_INTERVAL_MS   10000    // Send telemetry every 10 seconds

// Boot sequence delays (prevent brownout)
#define BOOT_SAFE_DELAY_STAGGER_MS  1000
#define BOOT_SAFE_DELAY_USB_MS      5000

// Power management
#define BATTERY_CRIT_VOLTAGE    3.0f     // Critical threshold (volts)
#define BATTERY_WARN_VOLTAGE    3.3f     // Warning threshold
#define HEARTBEAT_NORMAL_MS     5000     // 5s interval in NORMAL mode
#define HEARTBEAT_CONSERVE_MS   15000    // 15s in CONSERVE mode
#define HEARTBEAT_CRITICAL_MS   60000    // 60s in CRITICAL mode

// Mesh routing
#define MAX_HOPS_ALLOWED        4        // Maximum relay depth
#define DEDUP_BUFFER_SIZE       16       // Packet deduplication rolling hash

// ============================================================================
// Debugging & Serial Output
// ============================================================================

#define DEBUG_ENABLED           1
#define DEBUG_SERIAL            Serial

// ============================================================================
// Compile-Time Info (for serial output)
// ============================================================================

#ifdef ROLE_HUB
  #define DEVICE_ROLE "HUB"
#else
  #define DEVICE_ROLE "NODE"
#endif

#ifdef RADIO_SX1276
  #define RADIO_MODEL "SX1276 (V2)"
#else
  #define RADIO_MODEL "SX1262 (V3/V4)"
#endif

#ifdef BOARD_HAS_PSRAM
  #define HAS_PSRAM_STR "Yes (2MB)"
#else
  #define HAS_PSRAM_STR "No"
#endif

// ============================================================================
// Static Assertions (Compile-Time Validation)
// ============================================================================

// Ensure max relay channels constant is used consistently
static_assert(MAX_RELAY_CHANNELS == 8, "MAX_RELAY_CHANNELS must be 8 for uint8_t bitmask");
