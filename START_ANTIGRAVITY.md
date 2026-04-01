# Start Antigravity Infrastructure (Simple)

**One command to start everything:**

```bash
cd /c/Users/spw1/Documents/Code/Antigravity
bash start-all.sh
```

## What Gets Started

### Core Services (docker-compose.yml)
- **Mosquitto MQTT** :1883 — device telemetry ingestion
- **PostgreSQL** :5432 — cache & config storage

### Phase 2 Services (plugins/_infrastructure/docker-compose.yml)
- **EMQX** :1884→1883 — MQTT broker with dashboard :18084→18083
- **InfluxDB** :8086 — time-series database
- **Telegraf** — MQTT→InfluxDB ingestion

### Applications
- **Dashboard** :5173 — React UI (npm run dev)
- **Daemon** :8001 — Python API
- **Test Pump** — Spoof telemetry data (runs once)

## Ports Overview

| Service | Port | Purpose |
|---------|------|---------|
| Mosquitto MQTT | 1883 | Device telemetry (real hardware) |
| PostgreSQL | 5432 | Config storage |
| EMQX MQTT | 1884 | Dev/testing MQTT |
| EMQX Dashboard | 18084 | MQTT admin UI |
| InfluxDB | 8086 | Time-series queries |
| Dashboard React | 5173 | Fleet UI |
| Daemon API | 8001 | Backend API |

## Manual Steps (if script fails)

```bash
# 1. Kill conflicting services
docker stop loralink_emqx emqx 2>/dev/null

# 2. Start core infrastructure
cd /c/Users/spw1/Documents/Code/Antigravity
docker-compose down --remove-orphans
docker-compose up -d

# 3. Start Phase 2 services
cd plugins/_infrastructure
docker-compose down --remove-orphans
docker-compose up -d

# 4. Start test data generator
cd /c/Users/spw1/Documents/Code/Antigravity/plugins/test-pump
python pump.py --scenario healthy_fleet &

# 5. Start React dashboard
cd /c/Users/spw1/Documents/Code/Antigravity/daemon/dashboard
npm install --legacy-peer-deps  # if needed
npm run dev

# 6. Start daemon (if needed)
cd /c/Users/spw1/Documents/Code/Antigravity/daemon
python src/main.py
```

## Verify It's Working

- **Dashboard:** http://localhost:5173
- **MQTT:** mosquitto_sub -h localhost -t "magic/+/telemetry"
- **InfluxDB:** http://localhost:8086
- **EMQX Dashboard:** http://localhost:18084 (admin/magic123)

## Production Pipeline (Untouched)

Your existing Cloudflare/HostGator setup remains independent. This infrastructure is isolated for development.
