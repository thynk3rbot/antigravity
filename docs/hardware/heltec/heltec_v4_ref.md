# Heltec WiFi LoRa 32 V4 - Definitive Reference

**Model**: Heltec WiFi LoRa 32 V4 (ESP32-S3R2 + SX1262)
**Revision**: v4.2 (Latest as of 2026)
**Manufacturer**: [Heltec Automation](https://heltec.org)

---

## 🚀 Technical Specifications

| Component | Specification | Notes |
| :--- | :--- | :--- |
| **MCU** | ESP32-S3R2 | Dual-core 240MHz Xtensa® 32-bit LX7 |
| **SRAM** | 512 KB | Internal |
| **PSRAM** | 2 MB | Integrated in S3R2 package |
| **Flash** | 16 MB | External SPI |
| **LoRa Radio** | SX1262 | High-performance sub-GHz radio |
| **Radio Specs** | 915MHz (ISM) | Output power up to +22dBm |
| **Display** | 0.96" OLED | SSD1306, 128x64 pixels |
| **Interface** | Native USB Type-C | Direct S3 USB support (CDC/JTAG) |
| **Battery** | 1.25mm SH1.25-2P | Lithium 3.7V - 4.2V |
| **Solar Input** | 1.25mm SH1.25-2P | Max input voltage 5V-6V |
| **Frequency** | 433/470/868/915 MHz | Hardware-dependent variants |

---

## 📌 Pin Mapping (GPIO)

| Function | GPIO Pin | Bus / Signal |
| :--- | :--- | :--- |
| **LoRa NSS (CS)** | 8 | SPI CS |
| **LoRa SCK** | 9 | SPI SCK |
| **LoRa MOSI** | 10 | SPI MOSI |
| **LoRa MISO** | 11 | SPI MISO |
| **LoRa RST** | 12 | Radio Reset |
| **LoRa BUSY** | 13 | High during radio operations |
| **LoRa DIO1** | **14** | **LoRa Interrupt** |
| **OLED SDA** | 17 | I2C Data |
| **OLED SCL** | 18 | I2C Clock |
| **OLED RST** | 21 | Display Reset |
| **Vext Control** | 36 | Low = Power ON, High = Power OFF |
| **Battery ADC** | **1** | **ADC1_CH0 (Pin 1)** |
| **User / PRG BTN** | 0 | Factory Bootloader / User Button |
| **UART0 RX** | 44 | Internal Debug UART |
| **UART0 TX** | 43 | Internal Debug UART |

---

## 🔌 Power Management & Subsystems

### Vext Control (GPIO 36)
`Vext` is a switchable power rail that controls the **OLED display** and the **external sensor voltage**.
*   **Enable**: `digitalWrite(36, LOW);`
*   **Disable**: `digitalWrite(36, HIGH);`

### Battery Voltage Sensing (GPIO 1)
To measure the battery voltage:
1. Ensure `Vext` is **LOW** (enabled) if the voltage divider is tied to it (check hardware revision).
2. Measure **ADC1_CH0 (GPIO 1)**.
3. Multiply by the divider ratio (default: **2.0f** or **6.6f** depending on firmware scale).

---

## 🔗 Official Resources
*   [Technical Wiki](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/)
*   [Pinout Diagram](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Pinmap/V4_pinmap.png)
*   [Datasheet (v4.2.0)](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf)
*   [Schematic Diagram](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic/WiFi_LoRa_32_V4.2.pdf)
