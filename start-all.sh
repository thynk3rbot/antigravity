#!/bin/bash
set -e

echo "=== Antigravity Infrastructure Startup ==="
echo ""

cd /c/Users/spw1/Documents/Code/Antigravity

# 1. Kill conflicting services
echo "Step 1: Cleaning up conflicting services..."
docker stop loralink_emqx 2>/dev/null || true
docker stop emqx 2>/dev/null || true
sleep 2

# 2. Start core infrastructure (Mosquitto + PostgreSQL)
echo "Step 2: Starting core infrastructure (Mosquitto + PostgreSQL)..."
docker-compose down --remove-orphans 2>/dev/null || true
docker-compose up -d
sleep 5

# 3. Start Phase 2 services (EMQX + InfluxDB + Telegraf)
echo "Step 3: Starting Phase 2 services (EMQX + InfluxDB + Telegraf)..."
cd plugins/_infrastructure
docker-compose down --remove-orphans 2>/dev/null || true
docker-compose up -d
sleep 10

# 4. Verify services are healthy
echo ""
echo "Step 4: Verifying services..."
echo "  Core services:"
docker ps --filter "name=magic_" --format "table {{.Names}}\t{{.Status}}"

echo ""
echo "  Phase 2 services:"
docker ps --filter "name=magic-" --format "table {{.Names}}\t{{.Status}}"

# 5. Start test pump
echo ""
echo "Step 5: Starting test pump (spoof data generator)..."
cd /c/Users/spw1/Documents/Code/Antigravity/plugins/test-pump
timeout 5 python pump.py --scenario healthy_fleet 2>&1 | head -5 || true
python pump.py --scenario healthy_fleet > /tmp/pump.log 2>&1 &
echo "  Test pump started in background (see /tmp/pump.log)"

# 6. Show URLs
echo ""
echo "=== Infrastructure Ready ==="
echo ""
echo "Access points:"
echo "  Dashboard:      http://localhost:5173    (React UI — start with: npm run dev)"
echo "  InfluxDB:       http://localhost:8086    (time-series DB)"
echo "  EMQX Admin:     http://localhost:18084   (MQTT dashboard, admin/magic123)"
echo "  Mosquitto MQTT: localhost:1883           (device telemetry)"
echo ""
echo "Next steps:"
echo "  1. cd daemon/dashboard && npm run dev      # Start dashboard"
echo "  2. cd daemon && python src/main.py         # Start daemon (if needed)"
echo "  3. mosquitto_sub -h localhost -t 'magic/+/telemetry'  # Monitor MQTT"
echo ""
