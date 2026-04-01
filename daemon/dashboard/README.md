# Magic Dashboard — Phase 3a Scaffold

React 19 + Vite + TypeScript + ECharts + MQTT.js + InfluxDB

## Quick Start

```bash
# Install dependencies
npm install

# Start dev server (localhost:5173)
npm run dev

# Build for production
npm build

# Preview production build
npm run preview
```

## Architecture

**Phase 3a (Current):**
- React 19 app with Vite bundler
- TypeScript for type safety
- Tailwind CSS with dark theme (matches cockpit.html)
- MQTT.js WebSocket client (connects to EMQX :8083)
- InfluxDB REST client for historical data queries
- Component library: Header, StatCard, (more to come)

**Phase 3b (Waits for infrastructure up):**
- Live MQTT telemetry binding → gauges/stats
- Historical timeseries from InfluxDB → line/bar charts via ECharts
- Plugin widget discovery + rendering
- Service tier filtering

## File Structure

```
daemon/dashboard/
├── src/
│   ├── services/
│   │   ├── mqtt.ts        ← MQTT client, topic subscription
│   │   └── influxdb.ts    ← InfluxDB REST queries
│   ├── hooks/
│   │   └── useMqtt.ts     ← React hooks for MQTT
│   ├── components/
│   │   ├── Header.tsx
│   │   └── StatCard.tsx
│   ├── App.tsx            ← Main dashboard app
│   ├── main.tsx           ← React root
│   └── index.css          ← Tailwind globals
├── index.html
├── vite.config.ts
├── tsconfig.json
├── tailwind.config.js
├── postcss.config.js
└── package.json
```

## Design System

Colors matching cockpit.html:
- Background: `#0a0a0f` (--bg-primary)
- Card: `#12121a` (--bg-card)
- Accent: `#00d4ff` (cyan, primary CTA)
- Secondary: `#7c3aed` (purple)
- Text: `#e2e8f0` (light gray)
- Muted: `#64748b` (muted gray)

## Dependencies

- `react@19` — UI framework
- `vite@5` — Bundler
- `typescript@5` — Type safety
- `tailwindcss@3` — Styling
- `echarts@5` — Charts (ready to integrate)
- `mqtt@5` — MQTT client
- `axios@1` — HTTP client for InfluxDB
- `lucide-react` — Icons
- `@radix-ui/*` — Headless UI components (future)

## Integration Checkpoints

1. **MQTT Connection:** Dashboard attempts to connect to `ws://localhost:8083`
   - Success: green dot in header
   - Failure: error message displayed

2. **Device Telemetry:** Once MQTT connected, awaits `magic/+/telemetry` messages
   - Auto-populates device list
   - Shows battery, signal, uptime per device

3. **InfluxDB Queries:** Once infrastructure is running
   - Will query historical battery/signal trends
   - ECharts integration for visualization

## Next Steps (Phase 3b)

- [ ] ECharts line chart for battery trends (1h/24h/7d)
- [ ] Plugin widget discovery from daemon API
- [ ] Service tier filtering (starter/pro/enterprise)
- [ ] Live MQTT gauge updates
- [ ] InfluxDB aggregation queries (avg, max, min)
- [ ] Responsive mobile layout
- [ ] Error boundary + retry logic

## Troubleshooting

**MQTT connection fails:**
- Ensure EMQX running: `docker compose ps` in `plugins/_infrastructure/`
- Check WebSocket port: 8083 (not 1883)
- Browser console for error details

**No telemetry appearing:**
- Start test-pump: `python plugins/test-pump/pump.py --scenario healthy_fleet`
- Check EMQX dashboard at http://localhost:18083 for topic traffic

**InfluxDB queries not working:**
- Ensure InfluxDB healthy: `docker logs magic-influxdb`
- Verify token: `INFLUXDB_TOKEN=magic-dev-token` in `.env`
- Check Telegraf is running and ingesting: `docker logs magic-telegraf`

## Configuration

Proxy settings in `vite.config.ts`:
- `/api` → `http://localhost:8001` (daemon)

Environment variables (create `.env` if needed):
```
VITE_EMQX_URL=ws://localhost:8083
VITE_INFLUXDB_URL=http://localhost:8086
VITE_INFLUXDB_TOKEN=magic-dev-token
VITE_INFLUXDB_ORG=magic
VITE_INFLUXDB_BUCKET=telemetry
```

(Currently hardcoded in services; can be made configurable)

