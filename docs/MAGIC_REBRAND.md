# Magic Rebrand & MagicCache Integration — Implementation Guide

## Overview

This document describes the complete Magic rebrand (from LoRaLink to Magic with octopus theming) and the integration of **MagicCache**, a real-time distributed cache system for mesh fleet data with alerting and export capabilities.

**Status:** ✓ All 17 tasks completed

---

## Part 1: Magic Rebrand

### 1.1 Branding Architecture

All branding is **client-facing only** and controlled via a centralized configuration file:

**File:** `daemon/branding.json`

```json
{
  "name": "Magic",
  "tagline": "The Octopus Network",
  "accent_color": "#7c3aed",
  "logo_path": "/static/images/magiclogo.png",
  "features": {
    "magiccache": true
  }
}
```

**Key Principle:** Branding is fetched dynamically at webapp startup via `/api/branding` endpoint and applied to the UI without requiring code changes.

### 1.2 Rebranding Changes

#### Daemon Changes

- **Class renamed:** `LoRaLinkDaemon` → `MagicDaemon` (`daemon/src/main.py`)
- **New endpoint:** `GET /api/branding` — returns centralized branding config
- **New endpoint:** `GET /api/services/status` — detects MagicCache service availability
- **Config update:** `daemon/config.json` — updated all service descriptions to "Magic" theme

#### Webapp Changes

**HTML Updates (`tools/webapp/static/index.html`):**
- Title: "Magic — Autonomous Mesh"
- Sidebar brand now displays Magic logo + "Magic" text
- Added Magic Cache nav item (💾 Magic Cache)
- Added dedicated MagicCache page with table browser UI

**CSS Theme Updates (`tools/webapp/static/css/shared.css`):**
- Primary accent color: Cyan `#00d4ff` → Purple `#7c3aed`
- Accent hover: `#00c3eb` → `#a855f7`
- Accent dim: `#0099b3` → `#6d28d9`
- Accent glow: `rgba(0, 212, 255, 0.4)` → `rgba(124, 58, 237, 0.4)`
- Updated all hardcoded accent colors (11 total occurrences)
- Added MagicCache-specific styling (table list, alerts, notifications)

**JavaScript Updates (`tools/webapp/static/js/app.js`):**
- Added `loadBrandingConfig()` function to fetch `/api/branding` on startup
- Added `applyBranding()` function to dynamically apply color/name changes
- Updated docstring: LoRaLink → Magic

**Assets:**
- Logo: `media/magiclogo.png` copied to `tools/webapp/static/images/magiclogo.png`
- Size: 5.1MB PNG (octopus-themed logo)

---

## Part 2: MagicCache Integration

### 2.1 Architecture Overview

**MagicCache** is a distributed, real-time cache service that:
1. **Listens to MQTT** for mesh device data
2. **Stores in-memory** with persistent write-through to PostgreSQL
3. **Exposes REST API** on port 8200 for webapp queries
4. **Broadcasts WebSocket** updates for real-time subscriptions
5. **Manages alerts** with condition-based triggering

**Service:**
- **Name:** `magic_lvc` (LVC = Live Volatile Cache)
- **Port:** 8200
- **Auto-start:** Enabled in `daemon/config.json`
- **Dependencies:** MQTT broker, optional PostgreSQL

### 2.2 REST API Specification

All endpoints are on `http://localhost:8200`

#### Health & Status

**GET /health**
```json
{
  "status": "healthy",
  "service": "magic-lvc",
  "tables": 5,
  "timestamp": "2026-03-30T15:30:00.000000"
}
```

#### Table Operations

**GET /tables** — List all tables
```json
{
  "tables": ["device_telemetry", "battery_alerts", "signal_strength"],
  "count": 3
}
```

**GET /tables/{name}** — Fetch entire table
```json
{
  "table": "device_telemetry",
  "records": {
    "node_1": {"battery": 85, "rssi": -75, "uptime": 3600},
    "node_2": {"battery": 42, "rssi": -88, "uptime": 7200}
  },
  "count": 2
}
```

#### Querying

**GET /query?table={name}&filters={json}** — Query with optional filters
```
GET /query?table=device_telemetry&filters={"battery":"<50"}
```

Response:
```json
{
  "table": "device_telemetry",
  "filters": "{\"battery\":\"<50\"}",
  "results": {
    "node_2": {"battery": 42, "rssi": -88}
  },
  "count": 1
}
```

#### Export

**POST /export?table={name}&format={json|csv}**

Formats: `json` or `csv`

```json
{
  "format": "json",
  "table": "device_telemetry",
  "data": {...},
  "export_timestamp": "20260330_153000"
}
```

#### Alert Management

**GET /alerts** — List all alerts
```json
{
  "alerts": {
    "low_battery": {
      "name": "Low Battery Alert",
      "condition": "battery < 20",
      "enabled": true,
      "triggered": false
    }
  },
  "count": 1
}
```

**POST /alerts** — Create alert
```json
{
  "name": "High Temperature",
  "condition": "temp > 85",
  "enabled": true
}
```

**PUT /alerts/{id}** — Update alert (enable/disable, change condition)

**DELETE /alerts/{id}** — Delete alert

#### Real-Time Updates

**WebSocket /ws** — Subscribe to real-time updates

Example subscription:
```javascript
const ws = new WebSocket('ws://localhost:8200/ws');
ws.send('subscribe:all');
```

Messages received:
```json
{
  "type": "update",
  "data": {
    "table": "device_telemetry",
    "node_id": "node_1",
    "battery": 85
  }
}
```

### 2.3 Alert Condition Syntax

Simple condition evaluator supports: `<`, `>`, `<=`, `>=`, `==`, `!=`

**Examples:**
- `"battery < 20"` — trigger if battery below 20%
- `"rssi < -90"` — trigger if signal strength drops below -90 dBm
- `"temperature > 85"` — trigger if temperature exceeds 85°C

**Evaluation:** Simple text parsing matches `field op value` and evaluates against incoming data.

### 2.4 Webapp Integration

#### Auto-Discovery

On webapp startup (`tools/webapp/static/js/app.js`):

1. Fetch `/api/services/status`
2. Check if `magiccache.enabled === true`
3. Determine port from `magiccache.port` (default 8200)
4. Test connectivity to `/health`
5. If available, initialize dashboard

#### Dashboard Features

**Location:** Sidebar nav item "💾 Magic Cache"

**Layout:**
- **Left sidebar:** Table list + Alert rules
- **Main content:** Selected table data + export controls

**Capabilities:**
- Browse all cached tables
- View individual table records (JSON format)
- Query with filters
- Export to JSON or CSV
- Create/manage alert rules
- Real-time WebSocket subscriptions for updates

**JavaScript Module:** `tools/webapp/static/js/magiccache.js`

Key functions:
- `initMagicCache()` — Check service availability
- `loadMagicCacheTables()` — Fetch table list
- `loadMagicCacheTable(name)` — Load table data
- `queryMagicCache(filters)` — Query with conditions
- `createMagicCacheAlert(name, condition)` — Create alert
- `connectMagicCacheWebSocket()` — Subscribe to real-time updates

#### Conditional Loading

MagicCache dashboard only loads if:
1. Service is running (port 8200 responds to `/health`)
2. Feature flag `magiccache: true` in `daemon/branding.json`
3. WebSocket connection succeeds

If unavailable, webapp continues normally without MagicCache.

---

## Part 3: Implementation Files

### Modified Files

| File | Changes | Task(s) |
|------|---------|---------|
| `daemon/branding.json` | Created | 1 |
| `daemon/src/main.py` | Renamed class, added /api/branding endpoint | 2 |
| `daemon/src/service_manager.py` | Updated docstring | 4 |
| `daemon/src/lvc_service.py` | Added REST API, alerts, WebSocket | 10-12 |
| `daemon/config.json` | Updated descriptions | 3 |
| `daemon/tray.py` | Updated class reference | (Earlier session) |
| `tools/webapp/static/index.html` | Added logo, title, MagicCache page | 6, 9, 14-15 |
| `tools/webapp/static/css/shared.css` | Theme colors, MagicCache styling | 7, 14 |
| `tools/webapp/static/js/app.js` | Branding loader | 8 |
| `tools/webapp/static/js/magiccache.js` | Created | 13-15 |
| `tools/webapp/static/images/magiclogo.png` | Copied from media | 9 |

### New Files

| File | Purpose | Task |
|------|---------|------|
| `daemon/branding.json` | Centralized branding config | 1 |
| `tools/webapp/static/js/magiccache.js` | Dashboard module | 13 |
| `tools/webapp/static/images/magiclogo.png` | Logo asset | 9 |

---

## Part 4: Testing & Deployment

### Manual Testing Checklist

#### Branding
- [ ] Webapp loads with title "Magic — Autonomous Mesh"
- [ ] Sidebar displays octopus logo and "Magic" text
- [ ] All UI elements use purple accent color (#7c3aed)
- [ ] `/api/branding` endpoint returns JSON config
- [ ] Branding changes in JSON are reflected in webapp without restart

#### MagicCache
- [ ] MagicCache service starts automatically with daemon
- [ ] `/health` endpoint responds with service status
- [ ] `/tables` lists available cached tables
- [ ] `/tables/{name}` returns table data
- [ ] `/query?table=X&filters=Y` works with JSON filters
- [ ] `/export` downloads CSV/JSON file
- [ ] WebSocket `/ws` accepts subscriptions
- [ ] Alerts can be created/updated/deleted
- [ ] Alert conditions are evaluated correctly

#### Integration
- [ ] Webapp detects MagicCache service (💾 Magic Cache nav item appears)
- [ ] Clicking nav item shows MagicCache dashboard
- [ ] Dashboard tables load and display correctly
- [ ] WebSocket updates reflect in real-time
- [ ] Alert notifications appear when conditions match

### Deployment Steps

1. **Start Daemon:**
   ```bash
   python daemon/src/main.py
   ```
   - Loads `branding.json`
   - Starts all auto services including `magic_lvc`
   - Exposes REST API on port 8001

2. **Start Webapp:**
   ```bash
   python tools/webapp/server.py
   ```
   - Loads from worktree or main repo
   - Fetches branding from daemon
   - Initializes MagicCache if available
   - Serves on port 8000

3. **Access Dashboard:**
   - http://localhost:8000
   - Look for "💾 Magic Cache" nav item
   - If present, service is detected

### Troubleshooting

**MagicCache dashboard doesn't appear:**
- Check `/api/services/status` — is `magiccache.enabled` true?
- Check daemon logs — did `magic_lvc` service start?
- Check network — can webapp reach localhost:8200?

**Branding not applied:**
- Verify `daemon/branding.json` syntax (valid JSON)
- Check `/api/branding` endpoint response
- Check browser console for JavaScript errors

**Alerts not triggering:**
- Verify alert condition syntax: `field op value`
- Check WebSocket connection in browser console
- Confirm data structure matches field names

---

## Part 5: Future Enhancements

### Phase 2 (Future)

1. **Third-Party Customization:**
   - Allow branding config to be loaded from external URL
   - Support multiple themes in single deployment
   - API for custom logo/color injection

2. **Advanced Alert System:**
   - Expression evaluator for complex conditions
   - Scheduled alerts (time-based)
   - Alert actions (Slack, email, webhook)

3. **MagicCache Extensions:**
   - Time-range queries
   - Aggregation (sum, avg, max, min)
   - Scheduled exports
   - Data retention policies

4. **Persistence:**
   - PostgreSQL integration for permanent storage
   - Data archive/retention management
   - Backup/restore

---

## Summary

✅ **Magic Rebrand:**
- Centralized branding config via `/api/branding`
- Octopus purple theme (#7c3aed) applied throughout
- Dynamic logo and name updates without code changes

✅ **MagicCache Integration:**
- REST API for table browsing, querying, export
- WebSocket real-time subscriptions
- Alert management with condition-based triggering
- Webapp dashboard with table explorer
- Conditional loading based on service availability

**Total Implementation:** 17 tasks, 9 commits, ~1500 lines of code

---

## Appendix: API Quick Reference

```bash
# Health check
curl http://localhost:8200/health

# List tables
curl http://localhost:8200/tables

# View table
curl http://localhost:8200/tables/device_telemetry

# Query with filters
curl "http://localhost:8200/query?table=device_telemetry&filters=%7B%22battery%22:%22%3C50%22%7D"

# Create alert
curl -X POST http://localhost:8200/alerts \
  -H "Content-Type: application/json" \
  -d '{"name":"Low Battery","condition":"battery < 20"}'

# List alerts
curl http://localhost:8200/alerts

# Export to CSV
curl "http://localhost:8200/export?table=device_telemetry&format=csv" > export.csv

# WebSocket subscription (JavaScript)
const ws = new WebSocket('ws://localhost:8200/ws');
ws.onmessage = (e) => console.log(JSON.parse(e.data));
ws.send('subscribe:all');
```

---

**Last Updated:** 2026-03-30
**Status:** Complete ✓
