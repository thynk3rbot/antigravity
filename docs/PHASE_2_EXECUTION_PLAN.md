# Phase 2 Execution Plan — Three Parallel Work Streams
**Date:** 2026-04-01
**Goal:** Deploy infrastructure, viai-site, and dashboard scaffold simultaneously
**Priority:** Measure twice (specs done ✓), cut once (execute fast) with alacrity

---

## Overview

Three independent work streams, minimal blocking dependencies. Infrastructure unblocks dashboard later; viai-site is independent.

```
Stream 1 (AG):     plugins/_infrastructure/              → docker-compose ✓ telegraf ✓ README
Stream 2 (AG/Claude): tools/viai-site/                   → FastAPI + vanilla JS (no deps)
Stream 3 (Claude):    daemon/dashboard/                  → React scaffold (waits for Stream 1 running)
```

---

## Stream 1: Shared Infrastructure (AG Priority — Foundational)

**What:** `plugins/_infrastructure/` — EMQX + InfluxDB + Telegraf

**Spec:** `docs/plans/2026-04-01-infrastructure-compose.md` ✓ (READ FIRST)

**Files to create:**
- `plugins/_infrastructure/docker-compose.yml` — EMQX 5.5, InfluxDB 2.7, Telegraf 1.30
- `plugins/_infrastructure/telegraf.conf` — MQTT consumer → InfluxDB ingest pipeline
- `plugins/_infrastructure/.env.example` — defaults
- `plugins/_infrastructure/README.md` — start/verify/test procedures

**Success:**
1. `docker compose up -d` starts all three healthy
2. EMQX dashboard at :18083
3. InfluxDB at :8086
4. Telegraf subscribes to `magic/+/telemetry` and `magic/+/status`
5. Running test-pump → data appears in InfluxDB

**Unblocks:** Dashboard (needs live InfluxDB data)

**Dependencies:** None (docker, docker-compose available locally)

---

## Stream 2: Customer Website (AG or Claude — High ROI, Zero Dependencies)

**What:** `tools/viai-site/` — Public-facing landing site

**Spec:** `docs/plans/2026-04-01-viai-site.md` ✓ (READ FIRST)

**Files to create:**
- `tools/viai-site/index.html` — Landing: hero, features, CTA
- `tools/viai-site/products.html` — Product showcase, fleet demo endpoint
- `tools/viai-site/customers.html` — Testimonials, success metrics, FAQ
- `tools/viai-site/docs.html` — Knowledge search via `/api/rag/search` (Dify)
- `tools/viai-site/style.css` — Dark theme, no frameworks
- `tools/viai-site/main.js` — Safe DOM manipulation, fleet status load, RAG search
- `tools/viai-site/server.py` — FastAPI on :8010, `/api/fleet-status`, `/api/rag/search`
- `tools/viai-site/Dockerfile` — Alpine Python 3.11, <100MB target
- `tools/viai-site/requirements.txt` — fastapi, uvicorn, httpx
- `tools/viai-site/.env.example` — DIFY_BASE_URL, DIFY_API_KEY

**Success:**
1. `python server.py` starts on :8010
2. `/` loads index.html
3. `/products.html` → `/api/fleet-status` returns demo or live devices
4. `/docs.html` → `/api/rag/search?q=...` queries Dify
5. All pages responsive (mobile, tablet, desktop)
6. No console errors, safe DOM (no innerHTML with untrusted data)
7. `docker build -t viai-site .` → <100MB image

**Unblocks:** Nothing — can deploy independently

**Dependencies:** Optional Dify integration (graceful fallback if unavailable)

---

## Stream 3: Product Dashboard Scaffold (Claude — Waits for Stream 1)

**What:** `daemon/dashboard/` — React app with plugin widget system

**Spec:** `docs/plans/2026-04-01-dashboard-and-quality-pipeline-design.md` Part 1 ✓ (READ FIRST)

**Phase 3a (Ready now):**
- React 19 + Vite setup with TypeScript
- shadcn/ui component library
- ECharts integration
- MQTT.js WebSocket setup (connects to EMQX :8083)
- InfluxDB REST client (queries :8086 timeseries)
- Plugin discovery API `/api/dashboard-widgets`
- Design system (--accent: #00d4ff, --bg-primary: #0a0a0f)

**Phase 3b (Waits for Stream 1 ✓):**
- Live MQTT subscriber (magic/+/telemetry → gauges/stats)
- InfluxDB timeseries queries (1h/24h/7d views)
- Plugin widget renderer (call widget.render() from manifest)
- Service tier filtering (show only plugins for customer tier)
- Test data from test-pump plugin

**Success:**
1. `npm run dev` starts dev server on :5173
2. Dashboard loads, connects to EMQX + InfluxDB
3. Live telemetry appears in stat cards/gauges
4. Historical data in line/bar charts
5. Plugin widgets render correctly
6. Service tier filtering works

**Unblocks:** Nothing — but needs Stream 1 running for full functionality

**Dependencies:** Stream 1 (EMQX + InfluxDB running)

---

## Stream 4 (Optional, Low Priority): Quality Pipeline (AG — Strategic)

**What:** `tools/quality/` — Ollama code review, grading, knowledge extraction

**Spec:** `docs/plans/2026-04-01-quality-pipeline.md` ✓ (READ FIRST)

**When:** After Streams 1–3 stabilize. This is strategic but not blocking.

---

## Dependency Graph

```
Stream 1: Infrastructure
    ↓ (enables real data)
Stream 3: Dashboard

Stream 2: viai-site (independent)
Stream 4: Quality Pipeline (independent)
```

**Critical path:** Stream 1 → Stream 3. Streams 2 & 4 can start anytime.

---

## Execution Order (Alacrity Focus)

### Round 1 — Queue AG immediately (parallel)
1. **AG Task 1:** Implement `plugins/_infrastructure/` from prompt
   - Reference: `docs/plans/2026-04-01-infrastructure-compose.md`
   - Exit criteria: `docker compose up -d` succeeds, all healthy
   - ETA: 1–2 hours

2. **AG Task 2:** Implement `tools/viai-site/` from prompt
   - Reference: `docs/plans/2026-04-01-viai-site.md`
   - Exit criteria: `python server.py` runs, all pages load, `/api/fleet-status` works
   - ETA: 1–2 hours
   - No blocking on Task 1

### Round 2 — Claude scaffolds dashboard (after AG confirms Stream 1 running)
3. **Claude Task 3:** Create `daemon/dashboard/` React app Phase 3a
   - Reference: `docs/plans/2026-04-01-dashboard-and-quality-pipeline-design.md` Part 1
   - Exit criteria: React app scaffolded, ECharts/MQTT.js/InfluxDB client integrated
   - ETA: 1 hour
   - Can start in parallel with AG Tasks 1–2 (just won't test live data yet)

### Round 3 — Integration (after Stream 1 confirmed healthy)
4. **Claude Task 4:** Implement dashboard Phase 3b (live data binding)
   - Start after Stream 1 is running
   - Connect MQTT live + InfluxDB timeseries queries
   - ETA: 1 hour

### Round 4 — Optional (if time permits)
5. **AG Task 3:** Implement `tools/quality/` from prompt
   - Reference: `docs/plans/2026-04-01-quality-pipeline.md`
   - ETA: 2–3 hours (lowest priority, most complex)

---

## Communication Protocol

**AG to Claude:**
- When Stream 1 (infrastructure) is running: "Infrastructure healthy, InfluxDB populated"
- When Stream 2 (viai-site) is deployed: "viai-site ready at localhost:8010"
- When roadblocks: State the exact error message, not an interpretation

**Claude to AG:**
- Commit all work immediately (no batching)
- Tag releases: `viai-site-v1`, `infrastructure-v1`, etc.
- No waiting for "perfect" — ship working incrementally

---

## Test Data Flow (Validation)

Once Streams 1 & 2 & 3 are live:

```bash
# Terminal 1: Start infrastructure
cd plugins/_infrastructure
docker compose up -d
docker compose ps  # Verify all healthy

# Terminal 2: Start viai-site
cd tools/viai-site
python server.py
# Open http://localhost:8010 → products.html → /api/fleet-status loads

# Terminal 3: Start test pump (generates MQTT telemetry)
cd plugins/test-pump
python pump.py --scenario healthy_fleet --interval 2
# Watch EMQX dashboard at http://localhost:18083 → Topics → magic/#

# Terminal 4: Start dashboard
cd daemon/dashboard
npm run dev
# Open http://localhost:5173 → dashboard loads MQTT + InfluxDB data

# Terminal 5: Verify Telegraf ingest
docker logs magic-telegraf | tail -20
# Should show MQTT subscription confirmations

# Terminal 6: Verify InfluxDB storage
# Open http://localhost:8086
# Explore → bucket:telemetry → should show data points from devices
```

---

## Success Criteria (Phase 2 Complete)

- [ ] **Stream 1:** `docker compose up -d` → all containers healthy, EMQX/InfluxDB/Telegraf running
- [ ] **Stream 2:** `localhost:8010` → site loads, all pages responsive, fleet demo works
- [ ] **Stream 3:** `localhost:5173` → dashboard loads, can connect to EMQX + InfluxDB (when Stream 1 running)
- [ ] **Integration:** test-pump → MQTT → Telegraf → InfluxDB → dashboard (full telemetry pipeline)
- [ ] **All code:** Committed to main with descriptive messages
- [ ] **All tags:** Applied (viai-site-v1, infrastructure-v1, dashboard-v1)

---

## Next Steps After Phase 2

1. **AG Phase 3:** Quality pipeline (`tools/quality/`) for code review/grading/knowledge extraction
2. **Claude Phase 4:** Dashboard Phase 3b (live data binding, plugin widget rendering)
3. **Both:** Stabilization, performance tuning, E2E testing via daemon `/api/test/e2e`
4. **Full UI:** Customer-facing tier display, admin controls, OTA firmware updates

---

**Estimated Total Time:** 4–6 hours for all three streams (working in parallel)
**Start:** NOW — commit messages already pushed to main, specs locked in ✓

