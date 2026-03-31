# Design: Magic Rebrand + MagicCache Integration

**Date:** 2026-03-30
**Status:** Approved for implementation
**Scope:** Full rebrand from Magic → Magic + MagicCache dashboard + branding system foundation

---

## 1. Overview

Complete rebrand of daemon and webapp from "Magic" to "Magic" (octopus-themed dark interface). Integrate MagicCache as an optional, decoupled service with REST API. Build foundation for third-party branding (late-bound configuration).

---

## 2. Architecture

### Layer 1: Branding Configuration
- `daemon/branding.json` — centralized branding source (name, colors, logo, theme)
- Loaded at daemon startup
- Served via `/api/branding` endpoint
- Applied by webapp at runtime (CSS variables, HTML elements)

### Layer 2: Daemon (Orchestration)
- Renamed `MagicDaemon` → `MagicDaemon`
- All user-facing messages use "Magic" branding
- Serves `/api/branding` endpoint
- Detects and announces enabled services (including MagicCache if running)

### Layer 3: Webapp (Client)
- Fetches branding config on startup
- Applies branding via CSS variables + HTML updates
- Displays Magic logo, colors (purples/blues), octopus theme
- Optional MagicCache dashboard panel (if service enabled)

### Layer 4: MagicCache Service (Decoupled)
- Existing `magic_lvc` service
- New REST API endpoints for queries, alerts, exports
- Real-time WebSocket for subscriptions
- Foundation for time-series and AI routing (future)

---

## 3. Branding System Design

### 3.1 Configuration File: `daemon/branding.json`

```json
{
  "name": "Magic",
  "tagline": "The Octopus Network",
  "description": "Autonomous mesh gateway & data orchestration",
  "accent_color": "#7c3aed",
  "secondary_color": "#a855f7",
  "tertiary_color": "#1f2937",
  "logo_path": "/static/magic-logo.svg",
  "favicon_path": "/static/magic-favicon.ico",
  "theme": "dark",
  "features": {
    "magiccache_enabled": true,
    "alerts_enabled": true,
    "export_enabled": true
  }
}
```

**Why this design:**
- Single source of truth for branding
- Easy to swap for third-party reskinning later
- Feature flags allow selective enablement of services

### 3.2 API Endpoint: `/api/branding`

**Request:**
```http
GET /api/branding
```

**Response:**
```json
{
  "name": "Magic",
  "tagline": "The Octopus Network",
  "accent_color": "#7c3aed",
  "logo_path": "/static/magic-logo.svg",
  "features": {
    "magiccache_enabled": true
  }
}
```

**Used by:** Webapp at startup to apply theming + feature detection

---

## 4. MagicCache REST API Design

**Base URL:** `http://localhost:8200/api` (configurable)

### 4.1 Tables & Queries

```http
GET /api/tables
```
Response: `[{name: "price_ticks", record_count: 1542, last_update: "2026-03-30T14:32:01Z"}, ...]`

```http
GET /api/query/{table}?limit=100&offset=0&filter={"symbol":"BTC"}
```
Response: Array of records

```http
GET /api/query/{table}/count?filter={"symbol":"BTC"}
```
Response: `{count: 523, table: "price_ticks"}`

### 4.2 Exports

```http
GET /api/export/{table}?format=csv&filter={"symbol":"BTC"}
```
Response: CSV file (attachment)

```http
GET /api/export/{table}?format=json&filter={"symbol":"BTC"}
```
Response: JSON array

### 4.3 Alerts

```http
POST /api/alerts
Content-Type: application/json

{
  "name": "BTC price spike",
  "table": "price_ticks",
  "condition": {"field": "price", "operator": ">", "value": 100000},
  "action": "webhook",
  "webhook_url": "http://localhost:8000/api/alert-notify"
}
```

```http
GET /api/alerts
```
Response: List of all alert rules

```http
POST /api/alerts/{id}/test
```
Response: `{success: true, message: "Alert triggered"}`

### 4.4 Real-Time Subscriptions

```javascript
const ws = new WebSocket('ws://localhost:8200/ws/subscribe/MagicCache/price_ticks');
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('New record:', data);
};
```

---

## 5. Daemon Changes

### 5.1 Class Rename
- `MagicDaemon` → `MagicDaemon`
- Update docstring: `Magic — the octopus. Autonomous mesh orchestrator.`

### 5.2 Configuration Files
- `daemon/config.json` — update service descriptions to Magic theme
- `daemon/branding.json` — new, centralized branding

### 5.3 Startup Messages
Replace all user-visible Magic references with Magic:
- `[Magic] ALL SYSTEMS NOMINAL 🐙`
- `[Magic] Dashboard: http://localhost:8000`
- Service descriptions use Magic branding

### 5.4 REST API Changes
- Add `GET /api/branding` — serve branding configuration
- Add `GET /api/services/status` — include MagicCache service status
- Existing endpoints unchanged

### 5.5 Files to Modify
- `daemon/src/main.py` — class rename, messages, /api/branding endpoint
- `daemon/src/service_manager.py` — update service descriptions
- `daemon/config.json` — update descriptions
- `daemon/branding.json` — new file
- `daemon/src/tray_manager.py` — update system tray messages if visible

---

## 6. Webapp Changes

### 6.1 Structure
- Rename `MagicListener` class → `MagicListener`
- Update all docstrings and class comments

### 6.2 HTML Changes (`tools/webapp/static/index.html`)
- Page title: `<title>Magic — Autonomous Mesh</title>`
- Sidebar: Magic logo (SVG or text wordmark)
- Favicon: Magic icon
- Main heading: "Magic Control Center"
- Remove/replace all Magic branding language

### 6.3 CSS Changes (`tools/webapp/static/css/`)
- Define branding variables (loaded from `/api/branding`):
  ```css
  :root {
    --accent: #7c3aed;
    --secondary: #a855f7;
    --bg-dark: #1f2937;
    --logo-url: url('/static/magic-logo.svg');
  }
  ```
- Update existing color scheme to purples/blues (octopus theme)
- Add octopus-themed styling (subtle wave patterns, purple accents)

### 6.4 JavaScript Changes (`tools/webapp/static/js/app.js`)

```javascript
// Fetch branding on startup
const brandingResp = await fetch('/api/branding');
const branding = await brandingResp.json();

// Apply to page
document.title = `${branding.name} — Mesh Control`;
document.documentElement.style.setProperty('--accent', branding.accent_color);
document.querySelector('.brand-logo').src = branding.logo_path;

// Conditional MagicCache panel
if (branding.features?.magiccache_enabled) {
  loadMagicCachePanel();
}
```

### 6.5 MagicCache Dashboard (`tools/webapp/static/js/magiccache.js`) — New File

**Features:**
- Table browser (list all cached tables)
- Record viewer (paginated records with filtering)
- Data export (CSV/JSON)
- Alert rule editor
- Real-time update indicator (WebSocket status)

**Tabs in main interface:**
1. Devices (existing)
2. Magic Cache (new, if enabled)

### 6.6 Files to Modify
- `tools/webapp/server.py` — class rename, /api/branding endpoint, MagicCache service detection
- `tools/webapp/static/index.html` — title, branding, sidebar
- `tools/webapp/static/css/style.css` (or equivalent) — octopus color theme
- `tools/webapp/static/js/app.js` — fetch branding, apply theme, load MagicCache conditionally
- `tools/webapp/static/js/magiccache.js` — new file for MagicCache dashboard

---

## 7. MagicCache Service Changes

### 7.1 REST API Implementation
- Add Flask/FastAPI app to `daemon/src/lvc_service.py`
- Port: 8200 (or configurable via config.json)
- Endpoints: tables, query, export, alerts, WebSocket subscription

### 7.2 Files to Modify
- `daemon/src/lvc_service.py` — add REST API server (new methods)
- `daemon/config.json` — add `magiccache_api_port: 8200`

---

## 8. Implementation Phases

### Phase 1: Core Rebrand (Daemon + Webapp Branding)
1. Create `daemon/branding.json` with Magic theme colors/logo
2. Rename daemon class, update all visible messages
3. Add `/api/branding` endpoint in daemon
4. Update webapp HTML/CSS for octopus theme
5. Fetch and apply branding in webapp JavaScript
6. Test: Daemon shows "Magic", webapp displays Magic branding

### Phase 2: MagicCache REST API
7. Implement REST API in `magic_lvc` service
8. Add alert management endpoints
9. Add WebSocket subscription handler
10. Test: Can query MagicCache via `/api/query`, get real-time updates

### Phase 3: MagicCache Dashboard UI
11. Build MagicCache panel in webapp
12. Add table browser, record viewer, export UI
13. Add alert rule editor
14. Conditional loading based on branding config

### Phase 4: Foundation for Third-Party Branding (Future-Proof)
15. Make branding file path configurable in daemon
16. Add logic for tenant-specific branding (future: `/api/branding?tenant=partner`)
17. Documentation for third-party customization

---

## 9. Success Criteria

### MVP Complete When:
- ✅ Daemon startup shows "Magic 🐙" branding
- ✅ Webapp title is "Magic — Autonomous Mesh" (or similar)
- ✅ Octopus color theme applied (purples/blues)
- ✅ All visible Magic references renamed to Magic
- ✅ MagicCache REST API functional (query, export, alerts)
- ✅ Webapp can query MagicCache (if enabled in config)
- ✅ MagicCache dashboard panel shows in webapp tabs
- ✅ Real-time updates via WebSocket working

---

## 10. Rollback Plan

If issues occur:
- Branding config not applied → Webapp falls back to default colors
- MagicCache API down → Webapp hides MagicCache panel gracefully
- Service rename breaks something → Revert class name, rebuild

**No breaking changes to REST API contracts** — all existing endpoints remain stable.

---

## 11. Future Extensions (Not MVP)

- **Time-series metrics:** Add Prometheus-style `/metrics` endpoint
- **AI routing:** Add intelligent alert routing based on ML models
- **Third-party themes:** Support custom branding uploads per tenant
- **Mobile app:** Mirror MagicCache dashboard to mobile UI
- **Audit logs:** Track all MagicCache queries for compliance

---

## 12. Files Summary

| File | Status | Change |
|------|--------|--------|
| `daemon/branding.json` | NEW | Branding configuration |
| `daemon/src/main.py` | MODIFY | Rename class, add /api/branding |
| `daemon/config.json` | MODIFY | Update descriptions |
| `daemon/src/service_manager.py` | MODIFY | Update service descriptions |
| `daemon/src/lvc_service.py` | MODIFY | Add REST API |
| `tools/webapp/server.py` | MODIFY | Rename class, branding detection |
| `tools/webapp/static/index.html` | MODIFY | Title, logo, branding |
| `tools/webapp/static/css/style.css` | MODIFY | Octopus color theme |
| `tools/webapp/static/js/app.js` | MODIFY | Fetch branding, apply theme |
| `tools/webapp/static/js/magiccache.js` | NEW | MagicCache dashboard |

---

## 13. Testing Checklist

- [ ] Daemon starts and shows "Magic" branding
- [ ] Webapp loads and fetches `/api/branding`
- [ ] CSS variables applied from branding config
- [ ] MagicCache tables visible in dashboard
- [ ] Can export data (CSV/JSON)
- [ ] Alert rules can be created/edited
- [ ] WebSocket real-time updates working
- [ ] All Magic references renamed
- [ ] No console errors or broken links

---

**Approval:** Ready for implementation
