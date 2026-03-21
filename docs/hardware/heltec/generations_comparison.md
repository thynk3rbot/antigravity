# Heltec WiFi LoRa 32 - Generations Comparison Reference

This document summarizes the technical differences between the V2, V3, and V4 generations of the Heltec WiFi LoRa 32 development boards.

---

## 🛠️ Hardware comparison

| Feature | **WiFi LoRa 32 V4** | **WiFi LoRa 32 V3** | **WiFi LoRa 32 V2** |
| :--- | :--- | :--- | :--- |
| **MCU** | ESP32-S3R2 (Dual-core 240MHz) | ESP32-S3FN8 (Dual-core 240MHz) | ESP32-D0WDQ6 (Dual-core 240MHz) |
| **SRAM / PSRAM** | 512KB SRAM + **2MB PSRAM** | 512KB SRAM (No PSRAM) | 512KB SRAM (No PSRAM) |
| **Flash** | 16MB (External SPI) | 8MB (Integrated) | 4MB or 8MB (V2.1) |
| **LoRa Radio** | **SX1262** | **SX1262** | **SX1276** |
| **USB Interface** | Native USB (Type-C) | CP2102 UART (Type-C) | CP2102 UART (Micro-USB) |
| **Display** | 0.96" OLED (SSD1306) | 0.96" OLED (SSD1306) | 0.96" OLED (SSD1306) |
| **Low Power** | < 10µA (Deep Sleep) | < 10µA (Deep Sleep) | ~800µA (Deep Sleep) |

---

## 📌 Pin Mapping (GPIO Comparison)

| Function | **V4 Pin** | **V3 Pin** | **V2 Pin** |
| :--- | :--- | :--- | :--- |
| **LoRa NSS (CS)** | 8 | 8 | 18 |
| **LoRa SCK** | 9 | 9 | 5 |
| **LoRa MOSI** | 10 | 10 | 27 |
| **LoRa MISO** | 11 | 11 | 19 |
| **LoRa RST** | 12 | 12 | 14 |
| **LoRa BUSY / DIO0** | 13 (BUSY) | 13 (BUSY) | 26 (DIO0) |
| **LoRa DIO1** | **14** | **14** | 35 (DIO1) |
| **OLED SDA** | 17 | 17 | 4 |
| **OLED SCL** | 18 | 18 | 15 |
| **OLED RST** | 21 | 21 | 16 |
| **Vext Control** | 36 | 36 | 21 |
| **Battery ADC** | **1 (ADC1_CH0)** | **1 (ADC1_CH0)** | 34 or 37 (V2.1) |
| **User / PRG BTN** | 0 | 0 | 0 |

---

## 🔗 Official Documentation Links

- **V4 Index**: [WiFi LoRa 32 V4 Documentation](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4/)
- **V3 Index**: [WiFi LoRa 32 V3 Documentation](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/)
- **V2 Index**: [V2 Hardware Update Log](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/hardware-update-log)
- **V4 Schematic (PDF)**: [v4.2 Schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic/WiFi_LoRa_32_V4.2.pdf)
- **V3 Schematic (PDF)**: [v3.2 Schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V3/WiFi_LoRa_32_V3.2_Schematic_Diagram.pdf)
- **V2 Schematic (PDF)**: [v2 Schematic](http://resource.heltec.cn/download/WiFi_LoRa_32/V2/WIFI_LoRa_32_V2_Schematic_diagram.pdf)
