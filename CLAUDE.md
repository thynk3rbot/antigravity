# Magic-AnyToAny — AI Context [V2]

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
| `MCPManager` | I2C GPIO Expander (MCP23017), ISR-driven | Wire (shared w/ OLED), `PIN_MCP_INT=38`; doc: `MCP23017.md` |
| `ProductManager` | Product deploy (pins + schedules + alerts atomically) | LittleFS `/products/`, NVS active product |

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
| `src/managers/MCPManager.cpp/h` | MCP pin range or API changed | `CommandManager.cpp` → `getPinFromName()`; `ScheduleManager.cpp` → `setupDynamicPin()` |

## Integration Logic

- **ESP-NOW:** Uses MAC-based addressing; callback signature: `(mac, data, len)`.
- **BLE:** GW-XXXX naming scheme; GATT server for raw serial terminal emulation.
- **OLED:** I2C-based; `VEXT` pin (36) must be `LOW` for power.

## Coding Standards & Gotchas

- **Indentation:** 2 spaces (Enforced by `.editorconfig`).
- **Includes:** Use bracket `<>` for global/system, quotes `""` for relative project files.
- **CommInterface:** Always use `COMM_` prefix (prevents collision with `SERIAL`).
- **Hardware Conflict:** Pin 14 is shared by `PIN_RELAY_12V_1` and `LORA_DIO1`—never enable both.

## Unified Workflow — One Branch, One Truth [CRITICAL]

**All Claude sessions and all IDEs work from `main`.** This is non-negotiable.

### Rules
1. **`main` is the single source of truth.** Both firmware versions, all tools, all docs live here.
2. **Feature branches are short-lived.** Branch from `main`, do the work, PR back within the same session or next day.
3. **Never accumulate parallel long-lived branches.** If a feature branch is >2 days old without a PR, something is wrong.
4. **Flash from `main` only.** All PlatformIO build environments target the `main` branch.
5. **`firmware/magic/`** is the single active firmware directory (renamed from v2). v1 has been retired.
6. **OTA deploy targets:** use mDNS — `pio run -e ota_master` or `pio run -e ota_slave` (resolves via `magic-<id>.local`, no hardcoded IPs)
7. **Single build, multi-flash** — build once, OTA to all devices; never build separately per device.

### Directory Layout (canonical)
```
main/
├── firmware/magic/  ← Active firmware (ESP32-S3 V3/V4, Mx framework)
│   ├── lib/App/     ← Application managers (boot, command, control, etc.)
│   ├── lib/Mx/      ← Mx framework (bus, queue, pool, LVC, transport)
│   ├── lib/HAL/     ← Hardware abstraction (radio, relay, sensor, MCP)
│   ├── lib/Transport/ ← Transport layer (LoRa, WiFi, BLE, MQTT, Serial)
│   └── src/main.cpp ← FreeRTOS task definitions
├── daemon/          ← PC daemon (Python, async Mx backend)
│   └── src/mx/      ← Python Mx framework
├── tools/           ← Webapp, version scripts, fleet tools
├── docs/            ← Plans, versioning, specs
├── .version         ← Version state file
└── CLAUDE.md        ← This file
```

### Git Contribution Rules
- **Never commit directly to `main`** — always work on `feature/<topic>` branches
- **Always PR feature branches → `main`** — use `/commit-push-pr` skill
- **Default branch on GitHub is `main`** — all PRs target `main`

## DevOps Procedures & Project Rules [CRITICAL]

Follow these rules for all builds, bugs, and enhancements:

- **Branding Consistency**: All branding/visual changes **must** propagate across Magic Fleet Admin (`tools/webapp/static/index.html`), Device Webserver (`src/managers/WiFiManager.cpp`), documentation (`docs/`), and external website.
- **Iterative UI Assessment**: Prototype on one page if needed to assess algorithm/results, but finalize across all platforms.
- **Bug Reporting**: Every bug must be preceeded by its **DEBUG ID**. Always reference the ID (e.g., `DASH-BATT`).
- **Feature Specification Rule**: If the USER requests a feature enhancement, the ASSISTANT **must** demand a formal specification in a standard, non-pedantic format. **Do not proceed** with implementation until the user complies with the request.
- **DevOps Workflow**: Refer to the [`devops.md`](file:///c:/Users/spw1/OneDrive/Documents/Code/Antigravity%20Repository/antigravity/.agents/workflows/devops.md) workflow for more details.

## Firmware Versioning [MANDATORY]

**Format:** `x.x.xxV3` or `x.x.xxV4` — separate counter per hardware target.

**Examples:** `0.0.15V3`, `0.0.17V4`

**Rules:**
- Version auto-increments on every `pio run -t upload` via `increment_version.py`
- Counters stored in `.version` at repo root: `V3=0.0.xx` and `V4=0.0.xx`
- V3 and V4 increment independently — flash V4 only, V4 counter goes up, V3 unchanged
- **Version = flash confirmation**: if `STATUS` returns same version as before, flash failed
- V2 hardware is **deprecated** — test machine only, not a production target
- After every successful flash, Claude commits `.version` and tags: `git tag v0.0.xxV3`

**Plugin internal versions:** Each firmware module/plugin carries a `PLUGIN_VERSION` constant
(e.g., `static const char* PLUGIN_VERSION = "1.0.0";`) — not tracked in git, available at runtime.

**Flash checklist (Claude must follow every time):**
1. Bump `FIRMWARE_VERSION` in `platformio.ini` to next version BEFORE flash
2. Update `.version` file to match
3. Run `pio run -t upload -e heltec_v4`
4. Confirm boot log shows new version string
5. Commit `platformio.ini` + `.version` + tag `vX.X.XVN`

## Three-Agent Team Process [MANDATORY]

Full process in `TEAM_PROCESS.md`. Summary:

**Roles:**
- **Claude** — Release engineer, architecture, daemon, tools. Owns all git operations.
- **AG** — Firmware coding, hardware flashing, test validation. Never commits directly.
- **Ollama** — Async boilerplate generation via `tools/multi-agent-framework/ollama_bridge.py`

**Session rules:**
1. AG always starts with `git pull origin main`
2. Queue Ollama tasks at session START (not end) — output ready in ~1 hour
3. AG reports hardware results to Claude in plain language — Claude commits + tags
4. Claude checks `git status` at session end — nothing uncommitted ever

**Ollama task queuing (AG: use this, don't hand-write boilerplate):**
```bash
python tools/multi-agent-framework/ollama_bridge.py generate-code "your task here"
python tools/multi-agent-framework/ollama_bridge.py analyze "paste code here"
python tools/multi-agent-framework/ollama_bridge.py search-replace "task description"
```

**What to offload to Ollama:** C++ structs, JSON boilerplate, test stubs, switch/case handlers,
CSS/HTML templates, SQLite CRUD, FastAPI route stubs, search/replace across files.

## OTA Firmware Delivery [MANDATORY WORKFLOW]

**ALWAYS use the device registry to flash. Never hardcode IPs. Never guess IPs.**

The registry exists so devices are identified by name, not address. If a device is
not in the registry, ADD IT — do not work around it with a raw IP.

**To flash a device:**
1. Look up device in registry: `GET /api/devices` — find by name or hardware_class
2. If missing: `POST /api/devices` to register it (device_id, hardware_class, ip_address)
3. Flash by device_id: `POST /api/ota/flash` `{"device_id": "Magic-XXXXXX"}`
4. Monitor: `GET /api/ota/status/{job_id}` — poll until done
5. Confirm: device STATUS returns new version number

**To flash all V4s at once:** `POST /api/ota/flash/by-class` `{"hardware_class": "V4"}`

**Registry API:**
- `GET  /api/devices`                    — list all devices
- `POST /api/devices`                    — register device
- `GET  /api/devices/{id}`               — get single device
- `POST /api/ota/flash`                  — flash single device by device_id
- `POST /api/ota/flash/by-class`         — flash all online devices of a class

**Never do this:** `pio run -t upload -e heltec_v4_ota_29` (hardcoded IP, bypasses registry)
**Always do this:** flash via daemon API using device_id from registry

## Shortcuts & Workflows

- `/build` - `pio run -e heltec_v4` (or heltec_v3)
- `/flash` - `pio run -t upload -e heltec_v4` — version auto-increments
- `/monitor` - `pio device monitor -b 115200`
- `/webapp` - `python tools/webapp/server.py` → `http://localhost:8000`
- `/commit` - Claude handles: stage → commit → tag → push
