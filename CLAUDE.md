# LoRaLink-AnyToAny — AI Context [V2]

Unified ESP32-S3 firmware focused on prioritized any-to-any command routing.

- **Board:** Heltec WiFi LoRa 32 V3 (ESP32-S3) | **Radio:** SX1262 915MHz
- **Build/Flash:** `pio run` / `pio run -t upload`
- **Path:** `C:\Users\spw1\.platformio\penv\Scripts\pio.exe`

## Architecture Highlights

- **Staggered Boot:** Core init sequence uses `delay()` to prevent brownout spikes during radio/WiFi/OLED power-up.
- **Singleton Hierarchy:** All managers in `src/managers/` use `getInstance()` static accessors.
- **Command Routing:** Central `CommandManager` routes messages from any `CommInterface` to any other based on target prefix.

## Core Managers

| Name | Responsibility | Critical Dependency |
| --- | --- | --- |
| `CommandManager` | Any-to-Any Message Routing | All Interfaces |
| `LoRaManager` | RadioLib SX1262, AES Encryption | SPI, PIN_LORA_CS |
| `ScheduleManager` | TaskScheduler, Priority Queue | Library: TaskScheduler |
| `WiFiManager` | Compact Web Dashboard, Config API, OTA | WebServer, LittleFS |
| `ESPNowManager` | Peer registry, RX queue, send/broadcast, NVS persistence | WiFi STA mode, `ESPNOW_MAX_PEERS=10`, queue=8 |
| `MQTTManager` | Telemetry & External Commands | WiFi, PubSubClient |
| `DataManager` | NVS Persistence, Node tracking | LittleFS, Preferences |

## Maintained Tools

PC-side tools in `tools/` are **first-class project artifacts** — not throwaway scripts. Any firmware change that affects a coupling point (commands, API endpoints, pins, limits) **must** also update the relevant tool file(s) in the same change.

| Tool | Entry Point | Purpose |
| --- | --- | --- |
| `tools/ble_instrument.py` | `python tools/ble_instrument.py [--device] [--ip] [--dry-run]` | BLE CLI: rotational command exercise, fills 5 SCHED slots, JSON session log |
| `tools/webapp/server.py` | `python tools/webapp/server.py [--device] [--ip]` → `http://localhost:8000` | PC control webapp: hybrid BLE+HTTP, 4-panel dashboard, WebSocket live updates |
| `tools/requirements.txt` | `pip install -r tools/requirements.txt` | Shared deps: bleak, rich, aiohttp, fastapi, uvicorn, websockets |

## Change Coupling — Firmware → Tools

When modifying firmware, check this table and update all listed tool files in the same commit.

| Firmware file changed | What changed | Tool files that must be updated |
| --- | --- | --- |
| `src/managers/CommandManager.cpp` | New command registered | `ble_instrument.py` → `COMMAND_GROUPS` dict; `webapp/static/index.html` → `GPIO_CONTROLS` if pin-related |
| `src/managers/WiFiManager.cpp` | New API endpoint or changed response field | `webapp/server.py` → add/update proxy route; `webapp/static/index.html` → JS status renderer |
| `src/managers/ScheduleManager.h` | `MAX_DYNAMIC_TASKS` value changed | `ble_instrument.py` → `FIRMWARE_MAX_TASKS` list length; `webapp/static/index.html` → `#s-pin` select options |
| `src/config.h` | Pin number or alias changed | `ble_instrument.py` → `FIRMWARE_MAX_TASKS` pin args (never use pin 14); `webapp/static/index.html` → `GPIO_CONTROLS` array |
| `src/managers/BLEManager.cpp` | UUID changed, or `notify()` begins being called | `ble_instrument.py` → `BLEConstants`; `webapp/server.py` → `ResponseBuffer` handling |
| `src/managers/ScheduleManager.cpp` | New task type added (beyond TOGGLE / PULSE) | `webapp/static/index.html` → `#s-type` `<select>` options; `ble_instrument.py` → `FIRMWARE_MAX_TASKS` if applicable |
| `src/managers/ESPNowManager.cpp/h` | New public API, peer struct changed, queue size changed | `webapp/server.py` → proxy routes; `webapp/static/index.html` → peer management panel if added |
| `src/managers/WiFiManager.cpp` serveHome() | Dashboard HTML changed | `webapp/static/index.html` → keep visual language in sync |

## Integration Logic

- **ESP-NOW:** Uses MAC-based addressing; callback signature: `(mac, data, len)`.
- **BLE:** GW-XXXX naming scheme; GATT server for raw serial terminal emulation.
- **OLED:** I2C-based; `VEXT` pin (36) must be `LOW` for power.

## Coding Standards & Gotchas

- **Indentation:** 2 spaces (Enforced by `.editorconfig`).
- **Includes:** Use bracket `<>` for global/system, quotes `""` for relative project files.
- **CommInterface:** Always use `COMM_` prefix (prevents collision with `SERIAL`).
- **Hardware Conflict:** Pin 14 is shared by `PIN_RELAY_12V_1` and `LORA_DIO1`—never enable both.

## Git Workflow — Agent Contribution Rules

This project lives inside the `spw1` personal monorepo on GitHub. The **default branch for this project is `main`**.

| Branch | Purpose |
| --- | --- |
| `main` | Stable, tested, deployed firmware — **PR target** |
| `feature/<topic>` | Where agents and contributors work — always branch from `main` |

**Rules for agents:**
- **Never commit directly to `main`** — always work on `feature/<topic>` branches
- **Always PR feature branches → `main`** — use `/commit-push-pr` skill
- **OTA deploy targets:** use mDNS — `pio run -e ota_master` or `pio run -e ota_slave` (resolves via `loralink-<id>.local`, no hardcoded IPs)
- **Versioning is manual** — update `FIRMWARE_VERSION` in `src/config.h` only for meaningful releases, never auto-increment
- **Single build, multi-flash** — build once, OTA to all devices; never build separately per device

## Shortcuts & Workflows

- `/build` - `pio run -e heltec_wifi_lora_32_V3`
- `/flash` - Build + Upload + Monitor
- `/commit` - Verify build -> Stage -> Commit -> Push
- `/clean` - Clean build artifacts (`rmdir /s /q .pio`)
- `/monitor` - `pio device monitor -b 115200`
- `/webapp` - `python tools/webapp/server.py` → open `http://localhost:8000`
