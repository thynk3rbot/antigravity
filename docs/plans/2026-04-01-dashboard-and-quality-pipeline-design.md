# Magic V3 — Product Dashboard & Quality Pipeline Design

**Date:** 2026-04-01
**Status:** Approved
**Scope:** Two interconnected systems:
1. **Product Dashboard** — Plugin-aware React SPA with standardized instrumentation for OEM
2. **Quality Pipeline** — Recurring Ollama-powered review/grade/learn/teach cycle that builds institutional knowledge

---

## Part 1: Product Dashboard

### 1. Architecture

The dashboard is a plugin-aware single-page app served by the daemon. Each plugin contributes standardized widgets via `plugin.json`. Service tiers control which plugins (and therefore which widgets) a customer sees.

```
Browser (SPA)
  |
  |-- MQTT.js (WebSocket :8083) ----> EMQX (live telemetry)
  |-- REST API (:8086) -------------> InfluxDB (historical queries)
  |-- REST API (:8001) -------------> Daemon (plugin registry, device registry)
  |
  Dashboard reads plugin manifests --> renders widget grid
  Service tier config --> filters which plugins are visible
```

### 2. Stack (All MIT/Apache 2.0 — Zero Fees)

| Layer | Choice | License | Why |
|-------|--------|---------|-----|
| Framework | React 19 + TypeScript | MIT | Already scaffolded at `daemon/dashboard/` |
| Components | shadcn/ui (Radix + Tailwind) | MIT | Professional, customizable, copy-paste model |
| Charts | Apache ECharts 5 | Apache 2.0 | Best OSS charting for IoT, dark theme, 100K+ points |
| MQTT (live) | MQTT.js via WebSocket | MIT | Browser-native MQTT sub to EMQX :8083 |
| Time-series | InfluxDB OSS 2.x | MIT | Retention policies, downsampling, fast range queries |
| Ingest | Telegraf | MIT | MQTT -> InfluxDB bridge, zero code |
| Icons | Lucide React | ISC | Clean, consistent icon set (used by shadcn) |
| Layout | CSS Grid | N/A | Responsive widget grid |

### 3. Plugin Widget Protocol

Each plugin declares dashboard widgets in `plugin.json`:

```json
{
  "$schema": "magic-plugin-v1",
  "name": "lvc-service",
  "dashboard": {
    "widgets": [
      {
        "id": "fleet-status",
        "type": "table",
        "title": "Fleet Status",
        "size": "2x1",
        "data_source": {
          "type": "mqtt",
          "topics": ["magic/+/telemetry"],
          "transform": "flatten"
        }
      },
      {
        "id": "battery-trend",
        "type": "line-chart",
        "title": "Battery Trends",
        "size": "2x1",
        "data_source": {
          "type": "influxdb",
          "query": "from(bucket:\"magic\") |> range(start: -24h) |> filter(fn: (r) => r._measurement == \"telemetry\" and r._field == \"battery_mv\")"
        }
      }
    ]
  }
}
```

**Widget types** (standard library):

| Type | Renders | Data Source |
|------|---------|-------------|
| `stat` | Single value with label + trend arrow | MQTT (live) |
| `gauge` | Circular gauge (battery, signal) | MQTT (live) |
| `line-chart` | Time-series line chart | InfluxDB (historical) + MQTT (live append) |
| `bar-chart` | Bar chart | InfluxDB |
| `table` | Sortable data table | MQTT or REST |
| `map` | GPS positions on map | MQTT (live) |
| `status-grid` | Grid of device status cards | MQTT (live) |
| `log` | Scrolling log viewer | MQTT or WebSocket |
| `mqtt-explorer` | Topic browser + message inspector | MQTT (live) |
| `custom` | Plugin provides its own React component | Any |

**Widget sizing:** Grid-based. `1x1` = single cell, `2x1` = wide, `1x2` = tall, `2x2` = large.

### 4. Data Pipeline

```
ESP32 devices --> MQTT (EMQX :1883)
                     |
                     +--> Telegraf (subscriber) --> InfluxDB (historical)
                     |
                     +--> EMQX WebSocket (:8083) --> Browser (live widgets)
                     |
                     +--> RAG Router (:8403) --> Dify (analysis)
```

**Infrastructure** (single `docker-compose.yml` in `plugins/_infrastructure/`):

```yaml
services:
  emqx:
    image: emqx/emqx:5.5
    ports: [1883, 8083, 18083]

  influxdb:
    image: influxdb:2.7
    ports: [8086]
    volumes: [influxdb-data:/var/lib/influxdb2]

  telegraf:
    image: telegraf:1.30
    volumes: [./telegraf.conf:/etc/telegraf/telegraf.conf]
    depends_on: [emqx, influxdb]
```

**Telegraf config** (`telegraf.conf`):
```toml
[[inputs.mqtt_consumer]]
  servers = ["tcp://emqx:1883"]
  topics = ["magic/+/telemetry"]
  data_format = "json"
  topic_tag = "topic"

[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "${INFLUXDB_TOKEN}"
  organization = "magic"
  bucket = "telemetry"
```

### 5. Service Tiers & OEM

Service tiers are **plugin sets**. Each plugin declares its tier in `plugin.json`:

```json
{
  "tier": "pro",
  "tier_options": ["starter", "pro", "enterprise"]
}
```

The daemon reads the customer's tier from config and only starts/exposes plugins at or below that tier. The dashboard only renders widgets from active plugins.

| Tier | Plugins Included | Price Point |
|------|-----------------|-------------|
| **Starter** | test-pump, webapp (basic fleet view) | Free / low |
| **Pro** | + lvc-service, rag-router, alerting, battery analytics | Mid |
| **Enterprise** | + viai-testbed, custom widgets, white-label branding, API access | High |

**White-labeling:** A `branding.json` at the deployment root controls:
- Logo, app name, tagline
- Color palette (CSS variables injected at build or runtime)
- Which nav items appear
- Footer text, support URL

The webapp already has a branding API — the new dashboard inherits it.

### 6. Visual Design System

**Design language:** Industrial dark theme (consistent with existing cockpit.html aesthetic).

| Token | Value | Usage |
|-------|-------|-------|
| `--bg-primary` | `#0a0a0f` | Page background |
| `--bg-card` | `#12121a` | Widget cards |
| `--bg-elevated` | `#1a1a2e` | Hover states, modals |
| `--accent` | `#00d4ff` | Primary actions, chart highlight |
| `--accent-secondary` | `#7c3aed` | Secondary accent |
| `--success` | `#22c55e` | Online, healthy |
| `--warning` | `#f59e0b` | Low battery, degraded |
| `--danger` | `#ef4444` | Offline, critical |
| `--text-primary` | `#e2e8f0` | Body text |
| `--text-muted` | `#64748b` | Labels, secondary |

**Typography:** Inter (variable weight) — clean, professional, excellent at small sizes for data-dense UIs.

**Widget card pattern:**
```
+------------------------------------------+
| [icon]  Widget Title          [?] [...] |
|------------------------------------------|
|                                          |
|            Chart / Data / Map            |
|                                          |
|------------------------------------------|
| Last updated: 2s ago        [fullscreen] |
+------------------------------------------+
```

Cards use `border: 1px solid rgba(255,255,255,0.06)` and subtle `backdrop-filter: blur()` for glassmorphism — matching the existing cockpit aesthetic.

---

## Part 2: Quality Pipeline (Review / Grade / Learn / Teach)

### 7. Overview

A recurring automated pipeline that uses Ollama to:
1. **Review** — Scan recent commits against coding standards and safety checklists
2. **Grade** — Score code quality, test coverage, safety compliance on a rubric
3. **Learn** — Extract patterns, anti-patterns, and architectural decisions from the codebase
4. **Teach** — Generate lessons, best practices docs, and pattern catalogs that feed into RAG

```
Trigger (cron / pre-session / post-commit)
  |
  v
Gather (git diff, file reads, context)
  |
  v
Ollama Analysis (review, grade, extract)
  |
  v
Output (reports, lessons, pattern catalog)
  |
  v
Ingest into RAG (ChromaDB / Dify knowledge base)
  |
  v
Queryable Knowledge ("how did we handle X?")
```

### 8. Pipeline Tasks

#### Task 1: Commit Review (`review`)

**Trigger:** After each commit or batch of commits (daily cron or pre-session)
**Input:** `git log --since="24 hours ago" -p` (or since last review)
**Ollama prompt:**

```
You are a senior embedded systems and Python code reviewer for the Magic IoT platform.

Review the following commits against these standards:
- ESP32/FreeRTOS safety: no ISR violations, no portMAX_DELAY overflow, no cross-task mutex
- Python async safety: no blocking calls in async context, proper error handling
- MQTT contract compliance: topics match magic/{node_id}/telemetry format
- Immutability: new objects preferred over mutation
- Error handling: explicit, no silent swallows
- Security: no hardcoded secrets, validated inputs

For each file changed, output:
1. GRADE: A/B/C/D/F
2. ISSUES: list of problems with severity (CRITICAL/HIGH/MEDIUM/LOW)
3. PATTERNS: good patterns worth preserving
4. LESSONS: what should the team learn from this code

Commits:
{diff_content}
```

**Output:** `reports/YYYY-MM-DD-commit-review.md`

#### Task 2: Safety Audit (`audit`)

**Trigger:** Weekly or before any firmware flash
**Input:** All files in `firmware/magic/lib/` and `daemon/src/`
**Focus:** The 8-item ESP32 safety checklist + Python async patterns
**Output:** `reports/YYYY-MM-DD-safety-audit.md` with pass/fail per checklist item

#### Task 3: Pattern Extraction (`learn`)

**Trigger:** Weekly or when new modules are added
**Input:** Key source files (command_manager, mx_bus, wifi_mx_adapter, etc.)
**Ollama prompt:**

```
Analyze the following source files from the Magic IoT platform.

Extract:
1. ARCHITECTURAL PATTERNS — recurring design patterns (active object, message bus, etc.)
2. NAMING CONVENTIONS — how things are named, prefixes, suffixes
3. ERROR HANDLING PATTERNS — how errors flow through the system
4. MQTT TOPIC CONTRACT — all topic patterns and payload schemas
5. ANTI-PATTERNS — things that should not be repeated
6. COUPLING POINTS — where firmware and daemon/tools must stay in sync

Output as structured markdown suitable for a knowledge base.
```

**Output:** `knowledge/patterns.md`, `knowledge/anti-patterns.md`, `knowledge/coupling-map.md`

#### Task 4: Lesson Generation (`teach`)

**Trigger:** When a bug is fixed, or on demand
**Input:** The bug commit + fix commit + context
**Ollama prompt:**

```
A bug was found and fixed in the Magic IoT platform.

Bug: {description}
Root cause: {root_cause}
Fix: {fix_diff}

Generate a permanent lesson document that:
1. Explains what went wrong and WHY
2. Shows the incorrect pattern and the correct pattern side by side
3. Provides a checklist to prevent this class of bug
4. Names the lesson clearly (e.g., "ISR Safety: Never call portENTER_CRITICAL from ISR context")

This lesson will be indexed in our knowledge base and used to review future code.
```

**Output:** `knowledge/lessons/YYYY-MM-DD-{slug}.md`

### 9. Pipeline Runner

New file: `tools/quality/pipeline.py`

```python
"""
Magic Quality Pipeline — Recurring code review, grading, and knowledge extraction.

Usage:
    python pipeline.py review          # Review recent commits
    python pipeline.py audit           # Safety audit of firmware + daemon
    python pipeline.py learn           # Extract patterns from codebase
    python pipeline.py teach <commit>  # Generate lesson from a bug fix
    python pipeline.py all             # Run review + audit + learn
    python pipeline.py ingest          # Push reports/knowledge into RAG
"""
```

**Architecture:**

```python
class QualityPipeline:
    def __init__(self, config):
        self.ollama = OllamaClient(config)  # Wraps ollama_bridge
        self.rag = RAGIngestor(config)      # Wraps rag/ingest.py
        self.repo = GitRepo(config)         # git log, diff, file reads

    def review(self, since="24h"):
        """Review commits since last review."""
        diff = self.repo.get_diff(since)
        result = self.ollama.analyze(REVIEW_PROMPT, diff)
        self.save_report("commit-review", result)

    def audit(self):
        """Safety audit of firmware and daemon."""
        files = self.repo.get_files(["firmware/magic/lib/", "daemon/src/"])
        result = self.ollama.analyze(AUDIT_PROMPT, files)
        self.save_report("safety-audit", result)

    def learn(self):
        """Extract patterns from codebase."""
        files = self.repo.get_key_files()
        result = self.ollama.analyze(LEARN_PROMPT, files)
        self.save_knowledge("patterns", result)

    def teach(self, bug_commit, fix_commit):
        """Generate lesson from a bug fix."""
        context = self.repo.get_bug_context(bug_commit, fix_commit)
        result = self.ollama.analyze(TEACH_PROMPT, context)
        self.save_lesson(result)

    def ingest(self):
        """Push all reports and knowledge into RAG."""
        self.rag.ingest_directory("reports/")
        self.rag.ingest_directory("knowledge/")
```

### 10. Scheduling & Automation

**Option A: Cron (simple, recommended for now)**

```bash
# Daily at 6 AM — review yesterday's commits, run before work session
0 6 * * * cd /path/to/antigravity && python tools/quality/pipeline.py review

# Weekly Sunday midnight — full audit + pattern extraction
0 0 * * 0 cd /path/to/antigravity && python tools/quality/pipeline.py all

# After every commit (git post-commit hook)
# .git/hooks/post-commit:
python tools/quality/pipeline.py review --since="1 commit"
```

**Option B: Daemon integration (future)**

The daemon discovers `plugins/quality-pipeline/plugin.json` and runs it on schedule. The pipeline becomes a first-class plugin with dashboard widgets showing review grades and trend lines.

### 11. RAG Integration

All pipeline outputs feed into two RAG systems:

1. **Local ChromaDB** (via `tools/multi-agent-framework/rag/ingest.py`)
   - For Claude and AG to query during development
   - "How did we handle ISR safety in MxPool?"
   - "What's the MQTT topic contract?"

2. **Dify Knowledge Base** (via `tools/rag_router/scripts/ingest_viai_knowledge.py` pattern)
   - For the RAG Router's HARDWARE domain expert
   - Customer-facing queries about device behavior
   - "Why is my device battery dropping fast?"

**Directory structure:**

```
tools/quality/
  pipeline.py          # Main runner
  prompts/
    review.md          # Commit review prompt template
    audit.md           # Safety audit prompt template
    learn.md           # Pattern extraction prompt template
    teach.md           # Lesson generation prompt template
  config.json          # Ollama model, RAG endpoints, file patterns
reports/               # Generated review reports (gitignored)
  2026-04-01-commit-review.md
  2026-04-01-safety-audit.md
knowledge/             # Extracted knowledge (committed to git)
  patterns.md
  anti-patterns.md
  coupling-map.md
  lessons/
    2026-04-01-isr-safety-portenter-critical.md
    2026-04-01-asyncio-queue-empty-not-real.md
    2026-04-01-portmax-delay-overflow.md
```

**Key distinction:** `reports/` is ephemeral (gitignored, regenerated). `knowledge/` is permanent (committed, versioned, accumulates over time).

### 12. Grading Rubric

Every review produces a grade per file and an overall grade per commit:

| Grade | Meaning | Criteria |
|-------|---------|----------|
| **A** | Exemplary | No issues. Good patterns. Well-tested. |
| **B** | Solid | Minor style issues only. Functional and safe. |
| **C** | Acceptable | Medium issues present. Works but could be better. |
| **D** | Needs Work | High-severity issues. Missing error handling or tests. |
| **F** | Fail | Critical safety violation, hardcoded secrets, or data loss risk. |

Grades are tracked over time. The dashboard (Part 1) can show a **Code Quality Trend** widget — a line chart of average grade per week. This creates accountability and visible improvement.

---

## Part 3: How They Connect

The dashboard and quality pipeline are two faces of the same system:

```
Quality Pipeline (Ollama)
  |
  | generates reports + knowledge
  |
  v
RAG (ChromaDB + Dify)
  |
  | queryable by dashboard, CLI, rag-router
  |
  v
Dashboard (React + ECharts)
  |
  | shows quality trends, latest review, lessons learned
  |
  v
Customer / Developer
```

**Plugin: `quality-pipeline`** — like test-pump, it's a self-contained plugin:

```json
{
  "$schema": "magic-plugin-v1",
  "name": "quality-pipeline",
  "display_name": "Code Quality Pipeline",
  "tier": "enterprise",
  "dashboard": {
    "widgets": [
      {"id": "quality-trend", "type": "line-chart", "title": "Code Quality Trend"},
      {"id": "latest-review", "type": "log", "title": "Latest Review"},
      {"id": "lessons-count", "type": "stat", "title": "Lessons Learned"}
    ]
  }
}
```

---

## Success Criteria

### Dashboard
1. `daemon/dashboard/` builds and serves a React SPA with shadcn/ui + ECharts
2. Dashboard reads plugin manifests and renders widget grid
3. Widgets connect to EMQX via WebSocket for live MQTT data
4. Widgets query InfluxDB for historical data
5. Service tier filtering works — hiding plugins above the customer's tier
6. Branding config changes logo, colors, app name

### Quality Pipeline
1. `python tools/quality/pipeline.py review` produces a graded commit review
2. `python tools/quality/pipeline.py learn` extracts patterns into `knowledge/`
3. `python tools/quality/pipeline.py teach` generates a lesson from a bug fix
4. `python tools/quality/pipeline.py ingest` pushes knowledge into ChromaDB
5. Knowledge is queryable: "how did we handle X?" returns relevant patterns
6. Grades trend over time and are visible in the dashboard

---

## What NOT To Build (YAGNI)

1. **Drag-and-drop dashboard editor** — fixed grid layout is sufficient for V1
2. **Real-time collaboration** — single-user dashboard for now
3. **Custom Grafana panels** — we own the charting, no Grafana dependency
4. **Ollama fine-tuning** — prompt engineering is sufficient; fine-tuning is Phase 4
5. **Multi-tenant auth** — OEM customers get their own deployment, not shared tenancy
