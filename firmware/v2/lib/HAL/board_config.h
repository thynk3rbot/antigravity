/**
 * @file board_config.h
 * @brief Magic v2 Hardware Abstraction Layer (HAL)
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

// Provide default definitions for IntelliSense/Linter if build flags are missing.
// This prevents #error directives from cluttering the IDE while still enforcing
// them during real builds via PlatformIO.
#if defined(__INTELLISENSE__) || defined(__clang__)
  #ifndef ROLE_HUB
    #ifndef ROLE_NODE
      #define ROLE_HUB
    #endif
  #endif
  #ifndef RADIO_SX1276
    #ifndef RADIO_SX1262
      #define RADIO_SX1276
      #define ARDUINO_HELTEC_WIFI_LORA_32
    #endif
  #endif
#endif

// Build Flag Validation (Compile-Time Checks)
// ============================================================================

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

// Hardware Identification
#if defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
  #define HW_VERSION "V4"
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
  #define HW_VERSION "V3"
#elif defined(ARDUINO_TTGO_T_BEAM_V1_1)
  #define HW_VERSION "TBEAM"
#else
  #define HW_VERSION "V2"
#endif

// ============================================================================
// LoRa Radio SPI Interface Pins
// ============================================================================

#ifdef RADIO_SX1276
  // Heltec V2 / T-Beam: SX1276 pinout
  #define LORA_MOSI     27
  #define LORA_MISO     19
  #define LORA_SCLK     5
  #define LORA_CS       18
#ifdef ARDUINO_TTGO_T_BEAM_V1_1
  #define LORA_RESET    23       // T-Beam V1.1 reset pin
#else
  #define LORA_RESET    14       // Heltec V2 reset pin
#endif
  #define LORA_DIO0     26
  #define LORA_DIO1     33       // For future FHSS support
  #define LORA_BUSY     -1       // V2/TBeam does not have BUSY pin
  #define LORA_FREQ_MHZ 915.0    // ISM band
  #define LORA_BW_KHZ   125.0    // Bandwidth

#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
  // Heltec V4: SX1262 pinout (Updated for V4 hardware)
  #define LORA_MOSI     10
  #define LORA_MISO     11
  #define LORA_SCLK     9
  #define LORA_CS       8        // V4 standard CS is 8
  #define LORA_RESET    12       // V4 standard RESET is 12
  #define LORA_DIO1     14       // V4 standard DIO1 is 14
  #define LORA_BUSY     13       // V4 standard BUSY is 13
  #define LORA_FREQ_MHZ 915.0
  #define LORA_BW_KHZ   125.0

#elif defined(RADIO_SX1262)
  // Heltec V3: SX1262 pinout (Standard V3)
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

#ifdef RADIO_SX1262
  // Heltec V3/V4: OLED on GPIO 17/18
  #define I2C_SDA                 17
  #define I2C_SCL                 18
#elif defined(ARDUINO_TTGO_T_BEAM_V1_1)
  // T-Beam V1.1: I2C (AXP192 + OLED) on GPIO 21/22
  #define I2C_SDA                 21
  #define I2C_SCL                 22
  #define PMIC_IRQ                35
#else
  // Heltec V2: OLED on GPIO 4/15
  #define I2C_SDA                 4
  #define I2C_SCL                 15
#endif

#define I2C_FREQ_HZ             400000
#define OLED_ADDRESS            0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#ifdef RADIO_SX1262
  #define OLED_RESET_PIN          21       // S3 boards (V3/V4) use GPIO 21 for OLED reset
#else
  #define OLED_RESET_PIN          16       // V2 uses GPIO 16
#endif

// ============================================================================
// Button Control (GPIO 0 - BOOT button, low-active with pull-high)
// ============================================================================

#define BUTTON_PIN              GPIO_NUM_0
#define BUTTON_DEBOUNCE_MS      50       // Debounce threshold (milliseconds)
#define BUTTON_LONG_PRESS_MS    2000     // Long press duration (milliseconds)
#define OLED_AUTO_ROTATE_MS     5000     // Auto-rotate pages every 5 seconds

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
  #define RELAY_CH6   255         // GPIO 21 shared with VEXT_PIN — disabled on V2
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

#ifdef RADIO_SX1262
  // Heltec V3/V4 (ESP32-S3): Battery sense on GPIO 1
  #define ADC_VBATT     1
#else
  // Heltec V2 (ESP32): Battery sense on GPIO 35
  #define ADC_VBATT     35
#endif

#define ADC_VBATT_DIV 6.6f      // Voltage divider ratio (Heltec standard)
#define ADC_TEMP      36        // ESP32 internal temperature sensor

// Battery voltage monitoring (Power Management)
// ADC pin configuration varies by board variant
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
  // Heltec V2 (ESP32): Battery voltage on GPIO 34 (ADC1_CH6)
  #define BAT_ADC_PIN 34
  #define BAT_ADC_UNIT 1         // ADC1
  #define BAT_ADC_CHANNEL ADC1_CHANNEL_6
#elif defined(ARDUINO_HELTEC_WIFI_LORA_32_V3) || defined(ARDUINO_HELTEC_WIFI_LORA_32_V4)
  // Heltec V3/V4 (ESP32-S3): Battery voltage on GPIO 1 (ADC1_CH0)
  #define BAT_ADC_PIN 1
  #define BAT_ADC_UNIT 1         // ADC1
  #define BAT_ADC_CHANNEL ADC1_CHANNEL_0
  #define BAT_ADC_CTRL 37        // V4 requires GPIO 37 HIGH for battery sense
#endif

#define BAT_ADC_VOLTAGE_DIVIDER 2.0f    // External divider: Vbat/2 = ADC input
#ifdef RADIO_SX1262
  #define VEXT_PIN              36       // V3 and V4 use GPIO 36 for VEXT control
#else
  #define VEXT_PIN              21       // V2 uses GPIO 21
#endif

// Battery voltage thresholds (in volts, actual cell voltage)
#define BAT_VOLTAGE_NORMAL_MIN 3.2f      // Minimum voltage for NORMAL mode
#define BAT_VOLTAGE_CONSERVE_MIN 2.8f    // Minimum voltage for CONSERVE mode (below = CRITICAL)

// ============================================================================
// GPS / GNSS UART Interface Pins
// ============================================================================
#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
  #define GPS_RX_PIN       39
  #define GPS_TX_PIN       38
  #define GPS_PPS_PIN      41
  #define GPS_RST_PIN      42
  #define GPS_WAKE_PIN     40
  #define GPS_EN_PIN       34       // Power enable (Active LOW)
#elif defined(ARDUINO_TTGO_T_BEAM_V1_1)
  #define GPS_RX_PIN       12       // ESP32 RX <- GPS TX
  #define GPS_TX_PIN       34       // ESP32 TX -> GPS RX
#endif

#define GPS_SERIAL_BAUD    9600

// ============================================================================
// System GPIO (Control, Status)
// ============================================================================

#define GPIO_PRG_BTN  0         // PRG button for factory reset (active-LOW)

// ============================================================================
// MCP23017 I2C GPIO Expander (Feature Parity Restoration)
// ============================================================================
#define MCP_PIN_BASE 100        // Native pins 0–99; MCP pins 100–227
#define MCP_CHIP_PINS 16        // 16 GPIO per chip (GPA0–GPB7)
#define MCP_MAX_CHIPS 8         // 8 addresses: 0x20–0x27
#define MCP_CHIP_ADDR_BASE 0x20 // I2C base address (A0=A1=A2=GND)

#ifdef ARDUINO_HELTEC_WIFI_LORA_32_V4
  #define PIN_MCP_INT   -1      // Collides with GPS_TX on V4
#else
  #define PIN_MCP_INT   38      // Standard for V3/V2 baseboard
#endif

#ifdef RADIO_SX1262
  #define GPIO_VEXT     36
#else
  #define GPIO_VEXT     21
#endif

// ============================================================================
// UART Serial Port (Debug, CLI)
// ============================================================================

#define SERIAL_BAUD   115200
#define SERIAL_RX     3         // RX pin (hardware)
#define SERIAL_TX     1         // TX pin (hardware)
#ifdef ARDUINO_HELTEC_WIFI_LORA_32
  #define PIN_LED     25        // Heltec V2 Onboard LED
#else
  #define PIN_LED     35        // Heltec V3/V4 Onboard LED
#endif

// ============================================================================
// Conditional: Hub-Only Features
// ============================================================================

// WiFi + OTA enabled on all boards (nodes need OTA for field updates)
#define ENABLE_WIFI_TRANSPORT
#ifndef RADIO_SX1276
  #define ENABLE_HTTP_API
#endif

#ifdef ROLE_HUB
  #define ENABLE_MQTT_TRANSPORT
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
#define WIFI_CONNECT_TIMEOUT_MS     1000    // 1s initial connect wait (prevent boot hang)
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
#define SERIAL_BAUD             115200

// ============================================================================
// Compile-Time Info (for serial output)
// ============================================================================

#define DEVICE_NAME             "peer"
#define HARDWARE_DESCRIPTION    "Magic v2 Peer"
#define DEVICE_ROLE             "Peer"

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
