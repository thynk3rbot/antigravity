# Infrastructure Docker Compose — AG Implementation Prompt

**Date:** 2026-04-01
**Prerequisites:** `git pull origin main`
**Goal:** Create the shared infrastructure stack (EMQX + InfluxDB + Telegraf) as a single docker-compose.yml that all plugins depend on.

---

## PROMPT START

You are creating the **shared infrastructure** for the Magic V3 plugin system. This is the foundation that every plugin depends on — the MQTT broker, time-series database, and data ingest pipeline. It lives at `plugins/_infrastructure/` and is managed by the daemon.

### Existing Code to Reference (READ FIRST)

1. **`docker-compose.production.yml`** (repo root) — Current production compose with EMQX. Use as reference for EMQX configuration, but the new compose replaces it.
2. **`docker-compose.yml`** (repo root) — Current dev compose with Mosquitto + Postgres. The new compose replaces Mosquitto with EMQX and adds InfluxDB + Telegraf.
3. **`daemon/src/mqtt_client.py`** — The daemon's MQTT client. Subscribes to `magic/+/telemetry`, `magic/+/status`, `magic/+/msg`. The Telegraf config must subscribe to the same topics.
4. **`plugins/test-pump/pump.py`** — Publishes to `magic/{node_id}/telemetry` and `magic/{node_id}/status`. Use as test data source.
5. **`docs/plans/2026-04-01-dashboard-and-quality-pipeline-design.md`** — Section 4 (Data Pipeline) is your spec.

### What You're Building

```
plugins/_infrastructure/
├── docker-compose.yml    ← EMQX + InfluxDB + Telegraf
├── telegraf.conf         ← MQTT → InfluxDB ingestion config
├── emqx/
│   └── acl.conf          ← MQTT access control (optional, can be empty)
├── .env.example          ← Configuration defaults
└── README.md             ← How to start/stop/verify
```

### docker-compose.yml

```yaml
version: "3.8"

services:
  emqx:
    image: emqx/emqx:5.5
    container_name: magic-emqx
    ports:
      - "1883:1883"       # MQTT TCP
      - "8083:8083"       # MQTT WebSocket (for browser clients)
      - "18083:18083"     # EMQX Dashboard (admin UI)
    environment:
      - EMQX_NAME=magic
      - EMQX_LOADED_PLUGINS=emqx_dashboard
      - EMQX_DASHBOARD__DEFAULT_USERNAME=admin
      - EMQX_DASHBOARD__DEFAULT_PASSWORD=magic123
    volumes:
      - emqx-data:/opt/emqx/data
      - emqx-log:/opt/emqx/log
    healthcheck:
      test: ["CMD", "emqx_ctl", "status"]
      interval: 10s
      timeout: 5s
      retries: 5
    restart: unless-stopped

  influxdb:
    image: influxdb:2.7
    container_name: magic-influxdb
    ports:
      - "8086:8086"       # InfluxDB HTTP API + UI
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=magic
      - DOCKER_INFLUXDB_INIT_PASSWORD=magic123
      - DOCKER_INFLUXDB_INIT_ORG=magic
      - DOCKER_INFLUXDB_INIT_BUCKET=telemetry
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=${INFLUXDB_TOKEN:-magic-dev-token}
    volumes:
      - influxdb-data:/var/lib/influxdb2
    healthcheck:
      test: ["CMD", "influx", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5
    restart: unless-stopped

  telegraf:
    image: telegraf:1.30
    container_name: magic-telegraf
    depends_on:
      emqx:
        condition: service_healthy
      influxdb:
        condition: service_healthy
    volumes:
      - ./telegraf.conf:/etc/telegraf/telegraf.conf:ro
    environment:
      - INFLUXDB_TOKEN=${INFLUXDB_TOKEN:-magic-dev-token}
      - MQTT_BROKER=emqx
      - MQTT_PORT=1883
    restart: unless-stopped

volumes:
  emqx-data:
  emqx-log:
  influxdb-data:
```

### telegraf.conf

This is the critical piece — it bridges MQTT into InfluxDB.

```toml
# Telegraf Configuration for Magic V3
# Subscribes to MQTT topics and writes to InfluxDB

[agent]
  interval = "5s"
  round_interval = true
  flush_interval = "10s"
  hostname = "magic-telegraf"

# ── Input: MQTT Consumer ──────────────────────────

[[inputs.mqtt_consumer]]
  servers = ["tcp://${MQTT_BROKER}:${MQTT_PORT}"]
  topics = [
    "magic/+/telemetry",
    "magic/+/status"
  ]
  qos = 0
  connection_timeout = "30s"
  client_id = "telegraf-magic"
  data_format = "json"

  # Extract node_id from topic: magic/{node_id}/telemetry
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "magic/+/telemetry"
    measurement = "telemetry"
    tags = "_/node_id/_"

  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "magic/+/status"
    measurement = "device_status"
    tags = "_/node_id/_"

# ── Output: InfluxDB v2 ───────────────────────────

[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "${INFLUXDB_TOKEN}"
  organization = "magic"
  bucket = "telemetry"
```

**What this does:** Every time a device (real or test-pump) publishes to `magic/{node_id}/telemetry`, Telegraf parses the JSON payload, extracts the `node_id` as a tag, and writes all numeric fields (battery_mv, battery_pct, rssi, uptime_ms, free_heap) into InfluxDB's `telemetry` measurement. The `node_id` tag enables per-device queries.

### .env.example

```
# Magic V3 Infrastructure Configuration
INFLUXDB_TOKEN=magic-dev-token
EMQX_DASHBOARD_PASSWORD=magic123
INFLUXDB_PASSWORD=magic123
```

### README.md

Write a short README covering:

1. **Start:** `docker compose up -d`
2. **Verify EMQX:** Open `http://localhost:18083` (admin / magic123)
3. **Verify InfluxDB:** Open `http://localhost:8086` (magic / magic123)
4. **Verify Telegraf:** Check `docker logs magic-telegraf` for subscription confirmation
5. **Test data flow:**
   ```bash
   # Terminal 1: Start test pump
   cd plugins/test-pump
   python pump.py --scenario healthy_fleet --interval 2

   # Terminal 2: Verify MQTT messages
   # Open EMQX Dashboard → Topics → magic/# should show traffic

   # Terminal 3: Verify InfluxDB storage
   # Open InfluxDB UI → Explore → bucket:telemetry → should show data points
   ```
6. **Stop:** `docker compose down`
7. **Reset:** `docker compose down -v` (deletes all data)

### Topic Contract Reference

These are the MQTT topics the infrastructure must handle. Telegraf subscribes to all of them:

| Topic Pattern | Publisher | Payload | InfluxDB Measurement |
|--------------|-----------|---------|---------------------|
| `magic/{node_id}/telemetry` | Firmware / test-pump | JSON: battery_mv, battery_pct, rssi, uptime_ms, neighbors, relay_1, relay_2, free_heap, version, gps | `telemetry` |
| `magic/{node_id}/status` | Firmware / test-pump | String: "ONLINE" or "OFFLINE" | `device_status` |
| `magic/{node_id}/msg` | Firmware | String: human-readable message | (not ingested — debug only) |
| `magic/cmd` | Dashboard / tools | String: command to execute | (not ingested — command channel) |
| `magic/{node_id}/response` | Firmware | String: command response | (not ingested — response channel) |

### Port Registry

| Port | Service | Purpose |
|------|---------|---------|
| 1883 | EMQX | MQTT TCP (devices, daemon, plugins) |
| 8083 | EMQX | MQTT WebSocket (browser dashboard) |
| 18083 | EMQX | EMQX admin dashboard |
| 8086 | InfluxDB | HTTP API + admin UI |

### Safety Checklist

- [ ] **EMQX healthcheck passes** before Telegraf starts (depends_on with condition)
- [ ] **InfluxDB healthcheck passes** before Telegraf starts
- [ ] **No hardcoded tokens** — all secrets via .env or environment variables
- [ ] **Volumes persist data** — `docker compose down` without `-v` preserves data
- [ ] **Telegraf topic_parsing extracts node_id correctly** — verify with test-pump
- [ ] **EMQX WebSocket port 8083 exposed** — required for browser MQTT.js connections
- [ ] **No port conflicts** with existing services (daemon :8001, webapp :8000, rag-router :8403)
- [ ] **EMQX dashboard credentials are not production defaults** — document that these must be changed for deployment

### What NOT To Do

1. **Do NOT include Mosquitto** — EMQX replaces it. Remove any references to Mosquitto.
2. **Do NOT include PostgreSQL** — InfluxDB replaces it for time-series. Postgres may return later for relational data but not in this compose.
3. **Do NOT modify any existing docker-compose files** — this is a new file in `plugins/_infrastructure/`.
4. **Do NOT add Grafana** — the product dashboard (React + ECharts) replaces Grafana. Grafana may be added as an optional ops plugin later.
5. **Do NOT add authentication to MQTT** — development mode. Auth is a Phase 4 concern.
6. **Do NOT overcomplicate Telegraf** — subscribe, parse JSON, write to InfluxDB. No transforms, no aggregations, no alerting.

### Build Verification

```bash
# Verify file structure
ls plugins/_infrastructure/docker-compose.yml
ls plugins/_infrastructure/telegraf.conf
ls plugins/_infrastructure/.env.example
ls plugins/_infrastructure/README.md

# Verify YAML is valid
python -c "import yaml; yaml.safe_load(open('plugins/_infrastructure/docker-compose.yml'))"

# Verify TOML is valid (telegraf.conf)
python -c "
try:
    import tomllib
    tomllib.loads(open('plugins/_infrastructure/telegraf.conf').read())
    print('TOML valid')
except:
    import tomli
    tomli.loads(open('plugins/_infrastructure/telegraf.conf').read())
    print('TOML valid')
"

# Start infrastructure
cd plugins/_infrastructure
docker compose up -d

# Wait for health checks
docker compose ps  # All should show "healthy"

# Test data flow
cd ../test-pump
pip install -r requirements.txt
python pump.py --scenario healthy_fleet --interval 2
# Check EMQX dashboard at http://localhost:18083 for traffic
# Check InfluxDB at http://localhost:8086 for stored telemetry

# Stop
cd ../plugins/_infrastructure
docker compose down
```

### Success Criteria

1. `plugins/_infrastructure/` directory exists with all files listed above
2. `docker compose up -d` starts EMQX + InfluxDB + Telegraf with no errors
3. All three containers pass health checks
4. EMQX dashboard accessible at `:18083`
5. InfluxDB UI accessible at `:8086`
6. Running test-pump → MQTT messages appear in EMQX dashboard
7. Running test-pump → telemetry data appears in InfluxDB `telemetry` bucket
8. `node_id` is correctly extracted as a tag in InfluxDB (per-device queries work)
9. `docker compose down && docker compose up -d` → data persists (volumes)
10. All 8 safety checklist items verified

## PROMPT END
