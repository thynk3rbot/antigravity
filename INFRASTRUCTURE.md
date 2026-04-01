# Antigravity Magic Infrastructure

## Quick Start

```bash
# Terminal 1: Infrastructure (stays running)
docker-compose up -d

# Terminal 2: Dashboard
cd daemon/dashboard && npm install --legacy-peer-deps && npm run dev

# Terminal 3: Test data (optional)
cd plugins/test-pump && python pump.py --scenario healthy_fleet
```

**Access:**
- 🎨 **Dashboard:** http://localhost:5173 (live fleet status + charts)
- 📊 **InfluxDB:** http://localhost:8086 (time-series queries)
- 📡 **MQTT:** localhost:1883 (device telemetry ingestion)
- 🗄️ **PostgreSQL:** localhost:5432 (config/cache storage)

---

## Architecture

### Data Flow
```
Device Firmware
    ↓ (MQTT publish)
Mosquitto :1883
    ↓ (subscribe)
Telegraf (ingest)
    ↓ (HTTP write)
InfluxDB :8086
    ↓ (REST query)
React Dashboard :5173
    ↓ (WebSocket live)
User sees live fleet status + historical charts
```

### Services (Standard Ports)

| Container | Image | Port | Purpose |
|-----------|-------|------|---------|
| magic_mqtt | eclipse-mosquitto:latest | 1883 | MQTT broker (device telemetry) |
| magic_db | postgres:15-alpine | 5432 | Config & cache storage |
| magic_timeseries | influxdb:2.7 | 8086 | Time-series database |
| magic_ingest | telegraf:1.30 | (internal) | MQTT→InfluxDB pipeline |

---

## Test Data

**3 simulated devices** (via test-pump):
- Magic-A3F2: Battery 85%, Signal -45 dBm
- Magic-B1E7: Battery 72%, Signal -62 dBm
- Magic-C4D9: Battery 91%, Signal -38 dBm

Publishes every 5 seconds to `magic/{node_id}/telemetry`

---

## Configuration Files

- **docker-compose.yml** — Service definitions & volumes
- **telegraf.conf** — MQTT input + InfluxDB output
- **daemon/config.json** — Daemon services (daemon, webapp, assistant)
- **daemon/dashboard/src/App.tsx** — React dashboard (live MQTT + InfluxDB queries)

---

## Monitor Data Flow

```bash
# Watch MQTT messages
mosquitto_sub -h localhost -t "magic/+/telemetry"

# Query InfluxDB (battery % over last 1 hour)
curl -s 'http://localhost:8086/api/v2/query?org=magic' \
  -H "Authorization: Token magic-dev-token" \
  -d '{"query":"from(bucket:\"telemetry\") |> range(start:-1h) |> filter(fn: (r) => r._measurement==\"telemetry\" and r._field==\"battery_pct\")"}'
```

---

## Production Pipeline

Your existing **EMQX production broker** (Cloudflare) remains separate and untouched:
- Dev/test uses Mosquitto :1883
- Prod uses EMQX (wherever configured)
- InfluxDB :8086 receives from both (or configure separately)

---

## Recreate from Scratch

```bash
# Clean slate
docker system prune -f --volumes

# Start fresh (same docker-compose.yml, telegraf.conf, code)
cd /c/Users/spw1/Documents/Code/Antigravity
docker-compose up -d
cd daemon/dashboard && npm install --legacy-peer-deps && npm run dev

# Dashboard should connect automatically to localhost:1883 and :8086
```

That's it. Fully reproducible, fully documented, simple.
