# Magic V3 — Daemon & Plugin Architecture Design

**Date:** 2026-04-01
**Status:** Design — pending approval
**Scope:** Product-grade daemon, plugin discovery, unified infrastructure

---

## Vision

One compiled C daemon in a Docker container. Discovers plugins from a filesystem directory. Manages shared infrastructure (EMQX). Exposes a REST API. Bridges to a native tray icon. Firmware talks to it via MQTT/HTTP — the existing API contract, unchanged.

The Python daemon is the prototype. The C binary is the product. Every design decision targets the C binary — no Python-specific patterns.

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                   Docker Container                   │
│                                                      │
│  ┌──────────────┐   ┌──────────────┐                │
│  │  Magic Daemon │   │    EMQX      │                │
│  │  (C binary)   │──▶│  (mqtt:1883) │                │
│  │               │   └──────────────┘                │
│  │  - Plugin     │                                   │
│  │    Discovery  │   ┌──────────────┐                │
│  │  - REST API   │──▶│  PostgreSQL   │  (optional)   │
│  │  - Process    │   └──────────────┘                │
│  │    Manager    │                                   │
│  │  - Health     │                                   │
│  │    Monitor    │                                   │
│  └──────┬───────┘                                   │
│         │ fork/exec                                  │
│  ┌──────┴───────────────────────────────┐           │
│  │          Plugin Processes             │           │
│  │  ┌─────────┐ ┌──────────┐ ┌────────┐│           │
│  │  │ webapp  │ │rag-router│ │  viai  ││           │
│  │  │ :8000   │ │ :8403    │ │ :8500  ││           │
│  │  └─────────┘ └──────────┘ └────────┘│           │
│  └──────────────────────────────────────┘           │
└─────────────────────────────────────────────────────┘
         │ localhost API
┌────────┴────────┐          ┌──────────────────┐
│  Tray Icon      │          │  Firmware (V3/V4) │
│  (native, host) │          │  via MQTT/HTTP     │
└─────────────────┘          └──────────────────┘
```

**Key principle:** The daemon is an orchestrator, not an application server. It starts things, stops things, checks health, and exposes status. It does not serve web pages, run RAG pipelines, or handle business logic. Plugins do that.

---

## 2. Directory Structure

```
plugins/                          ← plugin discovery root
├── _infrastructure/              ← reserved: shared infra (not a plugin)
│   ├── docker-compose.yml        ← ONE compose: EMQX + Postgres
│   ├── .env.example
│   └── README.md
│
├── webapp/                       ← plugin: Magic Fleet Dashboard
│   ├── plugin.json               ← self-describing manifest
│   ├── server.py
│   ├── requirements.txt
│   ├── static/
│   └── .env.example
│
├── rag-router/                   ← plugin: IoT-to-RAG via Dify
│   ├── plugin.json
│   ├── server.py
│   ├── requirements.txt
│   ├── knowledge/
│   └── .env.example
│
├── viai-testbed/                 ← plugin: viai.club test environment
│   ├── plugin.json
│   ├── ...
│   └── .env.example
│
├── lvc-service/                  ← plugin: Last Value Cache
│   ├── plugin.json
│   ├── lvc_service.py
│   └── .env.example
│
└── meshtastic-bridge/            ← plugin: Meshtastic telemetry bridge
    ├── plugin.json
    ├── bridge.py
    └── .env.example

daemon/                           ← daemon source (C target, Python shim)
├── src/
│   ├── main.c                    ← C daemon entry point (future)
│   ├── plugin_discovery.c        ← scan plugins/, parse plugin.json
│   ├── process_manager.c         ← fork/exec, health check, restart
│   ├── api_server.c              ← REST API (port 8001)
│   ├── mqtt_bridge.c             ← EMQX connection, firmware telemetry
│   ├── tray_bridge.c             ← localhost API for native tray icon
│   └── mx/                       ← Mx framework (Python, migrates to C)
├── shim/
│   ├── main.py                   ← Python shim (runs same contract)
│   ├── plugin_discovery.py       ← scan + parse (Python prototype)
│   ├── process_manager.py        ← subprocess management
│   └── tray_bridge.py            ← pystray host (runs on host, not container)
├── Dockerfile                    ← Alpine + C binary (future)
├── Dockerfile.shim               ← Python 3.12-slim (now)
└── config.json                   ← DEPRECATED — migrated to plugins/
```

---

## 3. Plugin Manifest — `plugin.json`

```json
{
  "$schema": "magic-plugin-v1",
  "name": "rag-router",
  "display_name": "RAG Router",
  "description": "IoT-to-RAG routing microservice via Dify",
  "version": "1.0.0",
  "author": "spw1",

  "run": {
    "cmd": "python server.py",
    "cwd": ".",
    "env_file": ".env",
    "language": "python",
    "requirements": "requirements.txt"
  },

  "port": 8403,
  "health": {
    "endpoint": "/health",
    "interval_s": 30,
    "timeout_s": 5,
    "retries": 3
  },

  "infrastructure": {
    "requires": ["mqtt"],
    "docker_compose": null
  },

  "auto_start": false,
  "restart_policy": "on-failure",

  "menu": {
    "group": "Services",
    "icon": "🔍",
    "url": "http://localhost:8403",
    "actions": [
      {"label": "Open Dashboard", "type": "url", "value": "http://localhost:8403"},
      {"label": "View Logs", "type": "action", "value": "logs"},
      {"label": "Restart", "type": "action", "value": "restart"}
    ]
  },

  "api": {
    "base_url": "http://localhost:8403",
    "endpoints": [
      {"method": "GET",  "path": "/health"},
      {"method": "POST", "path": "/api/query"}
    ]
  }
}
```

**Schema rules:**
- `"$schema": "magic-plugin-v1"` — version the manifest format for forward compat
- `"run.cwd": "."` — always relative to plugin directory
- `"infrastructure.requires"` — declares shared infra needs: `mqtt`, `postgres`, `redis`
- `"infrastructure.docker_compose"` — plugin-specific containers (null if none)
- `"health.endpoint"` — daemon polls this; plugin is "up" when it returns 200
- `"menu"` — tray icon auto-generates menu entries from this
- `"api"` — optional, for daemon to proxy or expose in its own API

---

## 4. Daemon Lifecycle

### Boot Sequence

```
1. Start shared infrastructure
   └── docker-compose up -d  (_infrastructure/docker-compose.yml)
   └── Wait for EMQX health on :1883
   └── Wait for Postgres health on :5432 (if configured)

2. Scan plugins/ directory
   └── For each subdirectory (skip _ prefixed):
       └── Read plugin.json
       └── Validate against schema
       └── Register in plugin registry (in-memory)

3. Start auto-start plugins (sorted by dependency)
   └── For each plugin where auto_start == true:
       └── Ensure infrastructure.requires are up
       └── cd to plugin dir
       └── exec run.cmd with env_file loaded
       └── Begin health check polling

4. Start REST API on :8001
   └── GET  /api/plugins          — list all discovered plugins + status
   └── POST /api/plugins/{name}/start
   └── POST /api/plugins/{name}/stop
   └── POST /api/plugins/{name}/restart
   └── GET  /api/plugins/{name}/logs
   └── GET  /api/plugins/{name}/health
   └── GET  /api/infrastructure    — shared infra status
   └── POST /api/infrastructure/restart

5. Start tray bridge (host-side, not in container)
   └── Connect to daemon API at localhost:8001
   └── Build menu from /api/plugins
   └── File watcher on plugins/ for hot-reload

6. Start file watcher on plugins/
   └── New directory + valid plugin.json → register, add to tray menu
   └── Deleted directory → stop plugin, remove from registry
   └── Modified plugin.json → reload config, restart if running
```

### Health Monitor (runs continuously)

```
Every health.interval_s per plugin:
  GET plugin.url + health.endpoint
  If fail:
    increment failure count
    If failure count >= health.retries:
      If restart_policy == "on-failure":
        kill process, restart
        log restart event
      Else:
        mark as "failed", update tray icon
  If success:
    reset failure count
    mark as "healthy"
```

---

## 5. Infrastructure Deduplication

**Problem:** Today there are 3 MQTT brokers (Mosquitto, EMQX in root compose, EMQX in rag-router compose) and no clear ownership.

**Solution:** One `_infrastructure/docker-compose.yml`:

```yaml
services:
  emqx:
    image: emqx/emqx:5.5
    container_name: magic_mqtt
    ports:
      - "1883:1883"     # MQTT TCP
      - "8083:8083"     # MQTT WebSocket
      - "18083:18083"   # EMQX Dashboard
    environment:
      EMQX_NAME: magic
      EMQX_ALLOW_ANONYMOUS: "true"
    volumes:
      - magic_emqx_data:/opt/emqx/data
    restart: unless-stopped

  postgres:
    image: postgres:15-alpine
    container_name: magic_db
    environment:
      POSTGRES_USER: ${DB_USER:-magic}
      POSTGRES_PASSWORD: ${DB_PASS:-magic}
      POSTGRES_DB: ${DB_NAME:-magic}
    ports:
      - "5432:5432"
    volumes:
      - magic_db_data:/var/lib/postgresql/data
    restart: unless-stopped

volumes:
  magic_emqx_data:
  magic_db_data:
```

**Rules:**
- Root `docker-compose.yml` and `docker-compose.production.yml` are DELETED
- `tools/rag_router/docker-compose.yml` EMQX section is DELETED (uses shared broker)
- Any plugin that declares `"infrastructure.requires": ["mqtt"]` gets `MQTT_BROKER=localhost` and `MQTT_PORT=1883` injected into its environment automatically
- The daemon is the only thing that runs `docker-compose up/down` on the infrastructure stack

---

## 6. Tray Icon Architecture

**Problem:** Tray icon runs via `pystray` which needs a GUI thread. Docker containers don't have GUI access.

**Solution:** Tray icon runs on the HOST as a thin native client. It connects to the daemon's REST API at `localhost:8001` and renders the menu from API data.

```
HOST                          CONTAINER
┌──────────────┐              ┌──────────────┐
│  Tray Icon   │───GET /api──▶│  Daemon API  │
│  (pystray)   │   :8001     │  :8001       │
│              │◀──JSON──────│              │
│  Builds menu │              │  Returns     │
│  from JSON   │              │  plugin list │
└──────────────┘              └──────────────┘
```

The tray binary is the ONE thing that runs outside the container. On Windows it's a Python script (pystray) or eventually a small C/Win32 tray app. On macOS it would be a menu bar app. On Linux, a system tray applet.

**Menu is fully dynamic** — built from `/api/plugins` response:
```json
{
  "plugins": [
    {
      "name": "webapp",
      "display_name": "Magic Dashboard",
      "status": "healthy",
      "port": 8000,
      "menu": {
        "group": "Services",
        "icon": "🌐",
        "url": "http://localhost:8000",
        "actions": [...]
      }
    }
  ],
  "infrastructure": {
    "mqtt": "healthy",
    "postgres": "healthy"
  }
}
```

---

## 7. Migration Path

### Phase 1: Plugin Discovery (Python shim)
- Create `plugins/` directory
- Move `tools/webapp/` → `plugins/webapp/` with `plugin.json`
- Move `tools/rag_router/` → `plugins/rag-router/` with `plugin.json`
- Move `daemon/src/lvc_service.py` → `plugins/lvc-service/` with `plugin.json`
- Write `daemon/shim/plugin_discovery.py` — scans, validates, registers
- Write `daemon/shim/process_manager.py` — replaces current `ServiceManager`
- Delete `daemon/config.json` services array (replaced by plugin.json files)
- Create `plugins/_infrastructure/docker-compose.yml`
- Delete root `docker-compose.yml` and `docker-compose.production.yml`
- Tray icon reads from daemon API, not hardcoded menu

### Phase 2: Container + Health
- `Dockerfile.shim` — Python daemon in Alpine container
- Shared infra starts inside container (or sidecar)
- Health check polling per plugin
- Auto-restart on failure
- Hot-reload via watchdog file watcher

### Phase 3: C Daemon
- Rewrite discovery, process manager, API server, MQTT bridge in C
- Libraries: `cJSON` (parse plugin.json), `libmicrohttpd` (REST API), `mosquitto` (MQTT client)
- Single static binary, ~2-3MB
- Same plugin.json contract — plugins don't change
- Python shim deleted

### Phase 4: Mx Integration
- Daemon publishes plugin lifecycle events to MxBus
- Plugins can subscribe to firmware telemetry via MxBus (not raw MQTT)
- MxTransport adapter bridges MQTT ↔ MxBus inside the daemon
- Wire format matches firmware exactly

---

## 8. What Gets Deleted

| Current file/directory | Disposition |
|---|---|
| `daemon/config.json` services array | Replaced by `plugins/*/plugin.json` |
| `daemon/src/service_manager.py` | Replaced by `daemon/shim/process_manager.py` |
| `daemon/src/infra_manager.py` | Replaced by `_infrastructure/` compose + daemon boot |
| `daemon/src/tray_manager.py` | Rewritten as thin API client |
| `docker-compose.yml` (root) | Deleted — merged into `_infrastructure/` |
| `docker-compose.production.yml` (root) | Deleted — merged into `_infrastructure/` |
| `tools/rag_router/docker-compose.yml` EMQX section | Deleted — uses shared broker |
| `tools/webapp/` | Moved to `plugins/webapp/` |
| `tools/rag_router/` | Moved to `plugins/rag-router/` |

---

## 9. Port Registry

| Port | Service | Owner |
|---|---|---|
| 1883 | MQTT (EMQX) | `_infrastructure` |
| 5432 | PostgreSQL | `_infrastructure` |
| 8000 | Magic Dashboard | `plugins/webapp` |
| 8001 | Daemon REST API | daemon |
| 8083 | MQTT WebSocket | `_infrastructure` |
| 8403 | RAG Router | `plugins/rag-router` |
| 18083 | EMQX Dashboard | `_infrastructure` |

Plugins declare their own port in `plugin.json`. Daemon enforces no collisions at registration time.

---

## 10. Security — End-to-End Encryption

### Current State

| Transport | Encryption | Status |
|---|---|---|
| LoRa (device ↔ device) | AES-128-GCM (mbedTLS) | ✅ E2E encrypted |
| ESP-NOW (device ↔ device) | AES-128-GCM (mbedTLS) | ✅ E2E encrypted |
| MQTT (device → daemon) | Plaintext JSON | ❌ Not encrypted |
| HTTP (device → daemon) | Plaintext JSON | ❌ Not encrypted |

### V3 Target: Encrypt the MxWire Layer

MQTT becomes a blind pipe. The broker never sees plaintext.

```
Device                                          Daemon
MxMessage → MxWire::serialize() → binary        binary → Crypto::decrypt() → MxWire::deserialize() → MxMessage
            → Crypto::encrypt()                           ↑
            → MQTT publish (opaque bytes)                 MQTT subscribe (opaque bytes)
```

**Implementation:**
- Firmware: `MxTransport(MQTT)::send()` calls `Crypto::encrypt()` on the serialized MxWire buffer before `mqtt.publish()`
- Daemon: `MxTransport(MQTT)::recv()` calls `Crypto.decrypt()` on the raw bytes before `MxWire.deserialize()`
- Key: AES-128-GCM, same key on both sides. Firmware loads from NVS (`NVSManager::getCryptoKey()`). Daemon loads from `_infrastructure/.env` (`MAGIC_CRYPTO_KEY=hex_string`).
- IV: Fresh 12-byte random per packet (already implemented in `Crypto::encrypt()`).
- AAD: MxWire header bytes (op, subject_id) — authenticated but not encrypted, so routing can happen without decryption if needed.
- Wire format: `[12-byte IV][encrypted MxWire payload][16-byte GCM tag]` — 28 bytes overhead per message.

**Self-describing format lives inside the encryption envelope.** MxRecord fields, dirty_mask deltas, subject IDs — all encrypted. The transport layer (MQTT) sees opaque bytes. The bus layer (MxBus) sees plaintext MxMessages after decryption. Plugins never touch crypto — they subscribe to MxBus subjects and get clean data.

**Key rotation:** `SETKEY` command on firmware + daemon config update. Both sides must agree. Mismatched keys = GCM auth failure = dropped packets (fail-safe, not fail-open).

---

## 11. UX — Dashboard & MQTT Management

### Principle

The EMQX dashboard (`:18083`) already provides full MQTT management — topic explorer, client list, message inspector, ACLs. We don't rebuild this. We **embed** it.

### Dashboard Integration

The Magic Dashboard (`plugins/webapp`) provides a unified view with embedded panels:

| Panel | Source | Purpose |
|---|---|---|
| Device Fleet | Daemon API `/api/plugins/lvc-service` | Live device status from MxRecord LVC |
| MQTT Explorer | EMQX Dashboard iframe or API proxy | Topic browser, message inspector |
| Plugin Status | Daemon API `/api/plugins` | Start/stop/health of all plugins |
| Relay Control | Daemon API → `CommandMxBridge` | GPIO/relay toggle with live feedback |
| Telemetry | MxBus subscription via WebSocket | Real-time charts (battery, RSSI, uptime) |
| RAG Query | Plugin API proxy to `:8403` | IoT question answering |

### Tray Icon Menu (Dynamic)

Built from `/api/plugins` + `/api/infrastructure`:

```
🐙 Magic
├── Dashboard           → localhost:8000
├── MQTT Explorer       → localhost:18083
├── ─────────────
├── Devices ▸
│   ├── 🟢 Magic-A3F2   (bat: 87%, RSSI: -42)
│   ├── 🟢 Magic-B1E7   (bat: 62%, RSSI: -58)
│   └── 🔴 Magic-C4D9   (last seen: 3m ago)
├── Services ▸
│   ├── ✅ Dashboard      [Stop]  :8000
│   ├── ✅ LVC Service    [Stop]
│   ├── ⬚ RAG Router     [Start] :8403
│   ├── ⬚ Assistant      [Start] :8300
│   └── ⬚ viai Testbed   [Start] :8500
├── Infrastructure ▸
│   ├── MQTT: 🟢 EMQX 5.5
│   ├── DB:   🟢 Postgres 15
│   └── [Restart All]
├── ─────────────
└── Exit
```

**Devices section** is populated from MxBus `HEARTBEAT` subject — daemon tracks last-seen timestamp per device. Green = heard in last 60s. Red = silent.

### Widget Library for Plugins

Plugins that serve web UIs can import shared dashboard components:

```
plugins/_shared/widgets/
├── device-card.js       ← device status card (name, battery, RSSI, relay state)
├── telemetry-chart.js   ← real-time chart (connects to daemon WebSocket)
├── relay-toggle.js      ← relay on/off switch (calls daemon API)
├── mqtt-topic-tree.js   ← topic browser (proxied from EMQX API)
└── status-badge.js      ← health indicator (green/yellow/red)
```

Any plugin imports `<script src="/_shared/widgets/device-card.js">` and gets a consistent, branded component. No duplication across plugin UIs.

---

## 12. Test Data Pump

For development and demo without live hardware:

```
plugins/test-pump/
├── plugin.json
├── pump.py              ← generates fake MxMessages at configurable rate
├── scenarios/
│   ├── healthy_fleet.json    ← 3 devices, normal telemetry
│   ├── low_battery.json      ← device going critical
│   ├── mesh_partition.json   ← network split scenario
│   └── relay_toggle.json     ← relay state changes
└── .env.example
```

The pump publishes to MQTT using the same wire format as real firmware. The daemon can't tell the difference. Useful for:
- Dashboard development without hardware
- Demo to customers (viai.club)
- Integration testing
- Load testing (crank up message rate)

---

## 13. Success Criteria

1. Drop a new directory in `plugins/` with a valid `plugin.json` → it appears in the tray menu within 10 seconds (hot-reload) or on next daemon restart
2. No code changes required to add a new plugin
3. One EMQX broker, one Postgres instance — shared, not duplicated
4. Firmware API contract unchanged — devices don't know the daemon was rewritten
5. `plugin.json` schema works identically for Python shim and future C daemon
6. Each plugin directory is self-contained: `git clone` + `plugin.json` = deployable
