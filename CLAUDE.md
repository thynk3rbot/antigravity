# LoRaLink-AnyToAny — AI Coding Context

> This file provides project context for AI coding assistants (Claude, Antigravity, Cursor, etc.)

## Project

**LoRaLink-AnyToAny** is a unified ESP32 firmware framework for the **Heltec WiFi LoRa 32 V3** board. It routes commands between any wireless interface (Serial, LoRa, BLE, WiFi, ESP-NOW) using a central `CommandManager` and priority-based `TaskScheduler`.

**Owner:** Steven P Williams ([spw1.com](https://spw1.com))

## Tech Stack

- **Platform:** ESP32-S3 (Heltec WiFi LoRa 32 V3)
- **Framework:** Arduino via PlatformIO
- **Radio:** SX1262 LoRa @ 915MHz (RadioLib)
- **Build:** `pio run -e heltec_wifi_lora_32_V3`
- **Upload:** `pio run -t upload`
- **Monitor:** `pio device monitor -b 115200`
- **PIO Path (Windows):** `C:\Users\spw1\.platformio\penv\Scripts\pio.exe`

## Architecture

```
src/
├── main.cpp              — Boot sequence, staggered init
├── config.h              — Pins, LoRa params, CommInterface enum, structs
├── crypto.h              — AES-128 encryption (mbedtls)
├── splash.h              — OLED splash screen
├── utils/DebugMacros.h   — Conditional LOG_PRINT/PRINTLN/PRINTF
└── managers/
    ├── CommandManager     — Central command router (any-to-any)
    ├── ScheduleManager    — TaskScheduler with prioritized tasks
    ├── DataManager        — NVS/LittleFS persistence, node tracking
    ├── LoRaManager        — SX1262 radio, encryption, dedup, repeater
    ├── ESPNowManager      — Peer management, queue-based RX
    ├── WiFiManager        — Web dashboard (/), config page (/config), OTA
    ├── BLEManager         — BLE GATT server, command queue
    └── DisplayManager     — SSD1306 OLED, 4 pages (Home/Net/Status/Log)
```

## Critical Gotchas

1. **CommInterface enum** uses `COMM_` prefix (`COMM_SERIAL`, `COMM_LORA`, `COMM_BLE`, `COMM_WIFI`, `COMM_ESPNOW`, `COMM_INTERNAL`) because Arduino.h `#define`s `SERIAL` as `0x0`
2. **ESP-NOW callback** uses legacy signature: `(const uint8_t *mac, const uint8_t *data, int len)` — NOT `esp_now_recv_info_t`
3. **All managers are singletons** accessed via `ManagerName::getInstance()`
4. **Staggered boot** prevents brownout resets — peripherals init with delays
5. **PIN_RELAY_12V_1 and PIN_LORA_DIO1 share pin 14** — hardware conflict, only one should be active
6. **VEXT pin (36)** must be LOW to power the OLED display

## Commands

`LED ON/OFF`, `BLINK`, `STATUS`, `READMAC`, `RADIO`, `SETNAME <n>`, `SETWIFI <ssid> <pass>`, `SETIP <ip>`, `ESPNOW ON/OFF`, `REPEATER ON/OFF`, `SLEEP <hrs>`, `GPIO <pin> <0/1>`, `READ <pin>`, `SETSCHED <ms>`, `GETSCHED`, `WIPECONFIG`, `<target> <cmd>`, `ALL <cmd>`

## Workflows (Antigravity)

- `/build` — Compile firmware
- `/flash` — Build + upload + monitor
- `/monitor` — Open serial monitor
- `/commit` — Build-verify then git commit & push
- `/clean` — Clean rebuild
