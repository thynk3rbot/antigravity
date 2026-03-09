# LoRaLink-AnyToAny

**Unified Any-to-Any Wireless Communication Framework for ESP32/Heltec V3**

> (c) 2026 Steven P Williams — [spw1.com](https://spw1.com)

## Overview

LoRaLink-AnyToAny is a modular firmware framework that enables **any wireless interface to communicate with any other**. Send a command via Bluetooth, and it routes to LoRa. Receive an ESP-NOW message, and it triggers a relay. Configure everything via a sleek web dashboard — all driven by a priority-based task scheduler.

## Supported Interfaces

| Interface | Range | Speed | Use Case |
|-----------|-------|-------|----------|
| **LoRa** | 1-10km | Low | Long-range mesh, telemetry |
| **ESP-NOW** | ~200m | High | Fast peer-to-peer, low-power |
| **BLE** | ~30m | Medium | Phone/tablet control |
| **WiFi** | LAN | High | Web dashboard, OTA, API |
| **Serial** | Wired | High | Debug, local terminal |

## Architecture

```
Serial ─┐
LoRa ───┤
BLE ────┼──▶ CommandManager ──▶ Local Actions (GPIO, Relay, Display)
WiFi ───┤                  ──▶ Forward to Network (LoRa, ESP-NOW)
ESP-NOW ┘
```

All interfaces feed into the **CommandManager**, which routes commands to local execution or broadcasts them to the mesh network. The **ScheduleManager** (TaskScheduler) handles priority-based task execution.

## Web Configuration

Navigate to the device IP in any browser:

- **Dashboard** (`/`) — Live status, protocol badges, message log, command input
- **Config** (`/config`) — Device name, WiFi, LoRa params, ESP-NOW peers, factory reset

## Quick Start

1. **Install** [PlatformIO](https://platformio.org/)
2. **Clone** this repo
3. **Build & Upload**: `pio run -t upload`
4. **Monitor**: `pio device monitor`

## Commands

| Command | Description |
|---------|-------------|
| `LED ON/OFF` | Toggle built-in LED |
| `STATUS` | Get device status (ID, battery, IP, RSSI) |
| `BLINK` | Flash LED 10 times |
| `SETNAME <name>` | Set device name (1-14 chars) |
| `SETWIFI <ssid> <pass>` | Configure WiFi credentials |
| `SETIP <ip>` / `SETIP OFF` | Static IP / DHCP |
| `ESPNOW ON/OFF` | Enable/disable ESP-NOW |
| `REPEATER ON/OFF` | Enable/disable LoRa repeater mode |
| `SLEEP <hours>` | Deep sleep (0.01-24h) |
| `GPIO <pin> <0/1>` | Set GPIO pin |
| `READ <pin>` | Read GPIO pin |
| `RADIO` | Dump LoRa diagnostics |
| `FPING <target>` | Protocol Failover Ping (Binary -> Text) |
| `SETSCHED <ms>` | Set 110V toggle interval |
| `GETSCHED` | Get dynamic schedule JSON |
| `WIPECONFIG` | Factory reset all settings |
| `<target> <cmd>` | Send command to specific node |
| `ALL <cmd>` | Broadcast command to all nodes |

## Hardware

- **Board**: Heltec WiFi LoRa 32 V3 (ESP32-S3)
- **Display**: 128x64 OLED (SSD1306)
- **Radio**: SX1262 LoRa @ 915MHz
- **Relays**: 4x GPIO-controlled (110V, 3x 12V)
- **Sensors**: DHT22 (optional)

## License

Proprietary — (c) 2026 Steven P Williams. All rights reserved.
