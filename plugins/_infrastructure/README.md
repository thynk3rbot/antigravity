# Magic V3 Unified Infrastructure

The shared backbone for the Magic V3 plugin system. Includes MQTT (EMQX), Time-Series DB (InfluxDB), and the ingestion bridge (Telegraf).

## Operational Flow
### 1. Start Infrastructure
```bash
docker compose up -d
```

### 2. Verify Services
- **EMQX Dashboard**: `http://localhost:18083` (admin / magic123)
- **InfluxDB Dashboard**: `http://localhost:8086` (magic / magic123)
- **Telegraf Logs**: `docker logs magic-telegraf`

### 3. Test Data Flow
```bash
# Start test pump
cd ../test-pump
python pump.py --scenario healthy_fleet

# Check InfluxDB Explore -> telemetry bucket for data
```

## Management
- **Stop**: `docker compose down`
- **Reset Data**: `docker compose down -v`
