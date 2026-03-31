# Magic Rebrand + MagicCache Integration — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete rebrand from Magic → Magic (octopus theme) + integrate MagicCache REST API + build dashboard UI + foundation for third-party branding.

**Architecture:**
- Centralized branding configuration (`daemon/branding.json`)
- Daemon serves `/api/branding` endpoint
- Webapp fetches branding at startup, applies via CSS variables
- MagicCache service exposes REST API + WebSocket
- Webapp conditionally loads MagicCache dashboard if enabled

**Tech Stack:**
- Daemon: Python, FastAPI/Flask (existing)
- Webapp: HTML/CSS/JavaScript, Fetch API, WebSocket
- MagicCache API: Python Flask/FastAPI, PostgreSQL (existing)
- Testing: Manual browser testing + curl for API

---

## Phase 1: Core Rebrand (Daemon + Branding System)

### Task 1: Create Branding Configuration File

**Files:**
- Create: `daemon/branding.json`

**Step 1: Create daemon/branding.json**

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

**Step 2: Verify file exists**

```bash
cat daemon/branding.json | python -m json.tool
```

Expected: Valid JSON output with no errors

**Step 3: Commit**

```bash
git add daemon/branding.json
git commit -m "feat: add Magic branding configuration"
```

---

### Task 2: Update Daemon Class & Add /api/branding Endpoint

**Files:**
- Modify: `daemon/src/main.py` (lines ~1-50, class definition, ~400+ startup messages, add endpoint)

**Step 1: Read current daemon/src/main.py to understand structure**

Look at:
- Class name: `MagicDaemon`
- Startup docstring
- `__init__` method
- Where FastAPI app is created
- Where startup messages are logged

**Step 2: Rename class and update docstring**

Find:
```python
class MagicDaemon:
    """Magic Daemon — orchestrates services."""
```

Replace with:
```python
class MagicDaemon:
    """Magic — the octopus. Autonomous mesh gateway orchestrator."""
```

**Step 3: Load branding.json in __init__**

In the `__init__` method, after other config loads, add:

```python
# Load branding configuration
with open('daemon/branding.json', 'r') as f:
    self.branding = json.load(f)
```

**Step 4: Update startup messages to use Magic branding**

Find all lines like:
```python
logger.info("[Magic] ...")
print("[Magic] ...")
```

Replace `[Magic]` with `[Magic]` and update messages:
- `[Magic] ALL SYSTEMS NOMINAL 🐙`
- `[Magic] Dashboard: http://localhost:8000`
- `[Magic] Services starting...`

**Step 5: Add /api/branding endpoint**

In the FastAPI app setup (where other routes are defined), add:

```python
@app.get("/api/branding")
def get_branding():
    """Return branding configuration for clients."""
    return self.branding
```

**Step 6: Test locally**

```bash
cd daemon
python src/main.py &
sleep 2
curl http://localhost:8001/api/branding | python -m json.tool
```

Expected: Returns branding JSON with Magic name, colors, logo paths

**Step 7: Verify startup messages**

Expected output in console:
```
[Magic] ALL SYSTEMS NOMINAL 🐙
[Magic] Dashboard: http://localhost:8000
[Magic] Services starting...
```

**Step 8: Commit**

```bash
git add daemon/src/main.py
git commit -m "feat: rename daemon to Magic, add /api/branding endpoint"
```

---

### Task 3: Update daemon/config.json Service Descriptions

**Files:**
- Modify: `daemon/config.json`

**Step 1: Open daemon/config.json and find service descriptions**

Look for lines like:
```json
"services": {
  "webapp": {
    "description": "Magic Fleet Control..."
  }
}
```

**Step 2: Update all service descriptions to Magic theme**

Replace:
- "Magic" → "Magic"
- Generic descriptions → octopus/mesh themed descriptions

Example:
```json
"webapp": {
  "description": "Magic Dashboard — fleet monitor & device control",
  "port": 8000
}
```

**Step 3: Update "home_base" or similar if present**

Find any place where daemon is described, update to:
```
"Magic Fleet Orchestrator"
```

**Step 4: Verify JSON is valid**

```bash
python -c "import json; json.load(open('daemon/config.json'))"
```

Expected: No errors

**Step 5: Commit**

```bash
git add daemon/config.json
git commit -m "feat: rebrand service descriptions to Magic"
```

---

### Task 4: Update daemon/src/service_manager.py

**Files:**
- Modify: `daemon/src/service_manager.py`

**Step 1: Find DEFAULT_SERVICES definition**

Look for lines like:
```python
DEFAULT_SERVICES = {
    "webapp": {
        "name": "Magic Dashboard",
        ...
    }
}
```

**Step 2: Update all service names and descriptions**

Replace:
```python
DEFAULT_SERVICES = {
    "webapp": {
        "name": "Magic Dashboard",
        "description": "Fleet monitor & device control",
        ...
    },
    "assistant": {
        "name": "Magic Assistant",
        "description": "Multi-domain AI assistant",
        ...
    }
}
```

**Step 3: Update any docstrings that reference Magic**

Find and replace:
```python
"""Service manager for Magic daemon"""
```

With:
```python
"""Service manager for Magic mesh orchestrator"""
```

**Step 4: Test syntax**

```bash
python -m py_compile daemon/src/service_manager.py
```

Expected: No syntax errors

**Step 5: Commit**

```bash
git add daemon/src/service_manager.py
git commit -m "feat: rebrand service manager to Magic"
```

---

## Phase 2: Webapp Branding System

### Task 5: Update daemon/src/main.py to detect MagicCache service status

**Files:**
- Modify: `daemon/src/main.py`

**Step 1: Add endpoint to report service status**

In the FastAPI app, add after `/api/branding`:

```python
@app.get("/api/services/status")
def get_services_status():
    """Return status of all services."""
    return {
        "services": self.service_manager.get_service_status(),
        "magiccache": {
            "enabled": "magic_lvc" in self.service_manager.running_services,
            "port": 8200
        }
    }
```

**Step 2: Test endpoint**

```bash
curl http://localhost:8001/api/services/status | python -m json.tool
```

Expected: Returns service statuses including magiccache_enabled flag

**Step 3: Commit**

```bash
git add daemon/src/main.py
git commit -m "feat: add /api/services/status endpoint for service detection"
```

---

### Task 6: Update Webapp HTML Title & Structure

**Files:**
- Modify: `tools/webapp/static/index.html`

**Step 1: Update page title**

Find:
```html
<title>Magic Cockpit</title>
```

Replace with:
```html
<title>Magic — Autonomous Mesh</title>
```

**Step 2: Update favicon reference**

Find:
```html
<link rel="icon" href="/static/magic-icon.ico">
```

Replace with:
```html
<link rel="icon" href="/static/magic-favicon.ico">
```

**Step 3: Update brand section in sidebar**

Find the brand/logo section (usually in header or sidebar), update:
```html
<div class="brand">
  <img id="brand-logo" src="/static/magic-logo.svg" alt="Magic" class="brand-logo">
  <div class="brand-text">Magic</div>
</div>
```

**Step 4: Update main heading**

Find and replace:
```html
<h1>Magic Fleet Control</h1>
```

With:
```html
<h1>Magic Control Center</h1>
```

**Step 5: Add div for MagicCache tab (will populate later)**

In the tabs section, add:
```html
<div id="tab-magiccache" class="tab-button" style="display:none;">
  Magic Cache
</div>
```

**Step 6: Verify HTML syntax**

```bash
python -m html.parser tools/webapp/static/index.html
```

Expected: No parsing errors

**Step 7: Commit**

```bash
git add tools/webapp/static/index.html
git commit -m "feat: rebrand webapp HTML to Magic"
```

---

### Task 7: Update Webapp CSS for Octopus Theme

**Files:**
- Modify: `tools/webapp/static/css/style.css` (or main stylesheet)

**Step 1: Find CSS variables/root section**

Look for:
```css
:root {
  --accent: ...
  --bg: ...
}
```

**Step 2: Update color scheme to octopus theme (purples/blues)**

Replace with:
```css
:root {
  --accent: #7c3aed;
  --secondary: #a855f7;
  --tertiary: #1f2937;
  --bg-dark: #0f172a;
  --bg-card: #1e293b;
  --border: #334155;
  --text-primary: #f1f5f9;
  --text-secondary: #cbd5e1;
}
```

**Step 3: Update any Magic-specific styling**

Search for `.magic`, `magic-` class names, rename to `.magic` or update references

**Step 4: Add octopus theme accents (optional but nice)**

Add subtle styling:
```css
.brand-logo {
  filter: drop-shadow(0 0 8px rgba(124, 58, 237, 0.5));
}

.header::before {
  background: linear-gradient(90deg, #7c3aed, #a855f7);
}
```

**Step 5: Test CSS validity**

```bash
python -c "import re; re.compile(open('tools/webapp/static/css/style.css').read())"
```

Expected: No major errors

**Step 6: Commit**

```bash
git add tools/webapp/static/css/style.css
git commit -m "feat: update webapp CSS to octopus theme (purples/blues)"
```

---

### Task 8: Update Webapp JavaScript to Fetch & Apply Branding

**Files:**
- Modify: `tools/webapp/static/js/app.js`

**Step 1: Add branding fetch at startup**

In the main initialization code, add at the very top:

```javascript
async function applyBranding() {
  try {
    const response = await fetch('/api/branding');
    const branding = await response.json();

    document.documentElement.style.setProperty('--accent', branding.accent_color);
    document.documentElement.style.setProperty('--secondary', branding.secondary_color);
    document.documentElement.style.setProperty('--tertiary', branding.tertiary_color);

    const logoElement = document.getElementById('brand-logo');
    if (logoElement) {
      logoElement.src = branding.logo_path;
      logoElement.alt = branding.name;
    }

    document.title = branding.name + ' — ' + branding.tagline;
    window.branding = branding;

    return branding;
  } catch (err) {
    console.error('Failed to load branding:', err);
    return null;
  }
}

applyBranding().then(branding => {
  if (branding && branding.features && branding.features.magiccache_enabled) {
    initMagicCachePanel();
  }
});
```

**Step 2: Check for syntax errors**

```bash
node --check tools/webapp/static/js/app.js
```

Expected: No syntax errors

**Step 3: Commit**

```bash
git add tools/webapp/static/js/app.js
git commit -m "feat: webapp fetches and applies branding configuration"
```

---

### Task 9: Copy Magic Logo Assets to Webapp

**Files:**
- Check: `media/magiclogo.png` (from user notes)
- Create: `tools/webapp/static/magic-logo.svg`
- Create: `tools/webapp/static/magic-favicon.ico`

**Step 1: Check if logo exists**

```bash
ls -la media/magiclogo.png
```

**Step 2: Create SVG wrapper if needed**

Create `tools/webapp/static/magic-logo.svg`:

```svg
<?xml version="1.0" encoding="UTF-8"?>
<svg viewBox="0 0 200 200" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <style>
      .octopus-head { fill: #7c3aed; }
      .octopus-tentacle { stroke: #a855f7; stroke-width: 8; fill: none; stroke-linecap: round; }
      .octopus-eye { fill: white; }
    </style>
  </defs>
  <circle cx="100" cy="80" r="30" class="octopus-head"/>
  <path d="M 80 110 Q 70 150 75 190" class="octopus-tentacle"/>
  <path d="M 100 110 Q 100 160 100 200" class="octopus-tentacle"/>
  <path d="M 120 110 Q 130 150 125 190" class="octopus-tentacle"/>
  <circle cx="90" cy="75" r="4" class="octopus-eye"/>
  <circle cx="110" cy="75" r="4" class="octopus-eye"/>
</svg>
```

**Step 3: Create favicon**

```bash
echo "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==" | base64 -d > tools/webapp/static/magic-favicon.ico
```

**Step 4: Verify assets exist**

```bash
ls -la tools/webapp/static/magic-logo.svg
ls -la tools/webapp/static/magic-favicon.ico
```

Expected: Both files present

**Step 5: Commit**

```bash
git add tools/webapp/static/magic-logo.svg tools/webapp/static/magic-favicon.ico
git commit -m "feat: add Magic octopus logo assets"
```

---

## Phase 3: MagicCache REST API

### Task 10: Add REST API to MagicCache Service (magic_lvc)

**Files:**
- Modify: `daemon/src/lvc_service.py`

**Step 1: Read lvc_service.py to understand current structure**

Look at MQTT subscriptions, data persistence, current class methods

**Step 2: Add Flask imports**

```python
from flask import Flask, jsonify, request
from flask_cors import CORS
import threading
import json
```

**Step 3: Create Flask app in LVC service __init__**

```python
def __init__(self):
    # ... existing code ...
    self.app = Flask('magiccache-api')
    CORS(self.app)
    self._setup_routes()

def _setup_routes(self):
    """Register Flask routes."""

    @self.app.route('/api/tables', methods=['GET'])
    def get_tables():
        try:
            tables = self._get_table_list()
            return jsonify({"tables": tables, "count": len(tables)})
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @self.app.route('/api/query/<table>', methods=['GET'])
    def query_table(table):
        try:
            limit = request.args.get('limit', 100, type=int)
            offset = request.args.get('offset', 0, type=int)
            records = self._query_table(table, limit, offset)
            return jsonify({"table": table, "records": records, "count": len(records)})
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @self.app.route('/api/export/<table>', methods=['GET'])
    def export_table(table):
        try:
            fmt = request.args.get('format', 'json')
            if fmt == 'csv':
                csv_data = self._export_csv(table)
                return csv_data, 200, {'Content-Type': 'text/csv'}
            else:
                data = self._export_json(table)
                return jsonify(data)
        except Exception as e:
            return jsonify({"error": str(e)}), 500

def run_api_server(self, port=8200):
    """Run Flask API server in background thread."""
    def _run():
        self.app.run(host='0.0.0.0', port=port, debug=False)
    thread = threading.Thread(target=_run, daemon=True)
    thread.start()
```

**Step 4: Implement helper methods**

```python
def _get_table_list(self):
    with self.db_connection.cursor() as cursor:
        cursor.execute("""
            SELECT table_name FROM information_schema.tables
            WHERE table_schema = 'public'
        """)
        tables = []
        for row in cursor.fetchall():
            table = row[0]
            cursor.execute(f"SELECT COUNT(*) FROM {table}")
            count = cursor.fetchone()[0]
            tables.append({"name": table, "record_count": count})
        return tables

def _query_table(self, table, limit, offset):
    with self.db_connection.cursor() as cursor:
        cursor.execute(f"SELECT * FROM {table} LIMIT %s OFFSET %s", (limit, offset))
        columns = [desc[0] for desc in cursor.description]
        records = [dict(zip(columns, row)) for row in cursor.fetchall()]
        return records

def _export_csv(self, table):
    import csv, io
    with self.db_connection.cursor() as cursor:
        cursor.execute(f"SELECT * FROM {table}")
        columns = [desc[0] for desc in cursor.description]
        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=columns)
        writer.writeheader()
        for row in cursor.fetchall():
            writer.writerow(dict(zip(columns, row)))
        return output.getvalue()

def _export_json(self, table):
    records = self._query_table(table, limit=10000, offset=0)
    return {"table": table, "records": records}
```

**Step 5: Start API server on service startup**

In the service's `run()` method, add:

```python
self.run_api_server(port=8200)
```

**Step 6: Test API**

```bash
curl http://localhost:8200/api/tables | python -m json.tool
```

Expected: Returns list of tables

**Step 7: Commit**

```bash
git add daemon/src/lvc_service.py
git commit -m "feat: add REST API to MagicCache (tables, query, export)"
```

---

### Task 11: Add Alert Management Endpoints

**Files:**
- Modify: `daemon/src/lvc_service.py`

**Step 1: Create alerts table on startup**

```python
def _init_alerts_table(self):
    with self.db_connection.cursor() as cursor:
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS magiccache_alerts (
                id SERIAL PRIMARY KEY,
                name TEXT NOT NULL,
                table_name TEXT NOT NULL,
                condition JSONB NOT NULL,
                action TEXT NOT NULL,
                enabled BOOLEAN DEFAULT true,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        self.db_connection.commit()
```

Call in `__init__`: `self._init_alerts_table()`

**Step 2: Add alert endpoints**

```python
@self.app.route('/api/alerts', methods=['GET'])
def get_alerts():
    with self.db_connection.cursor() as cursor:
        cursor.execute("""
            SELECT id, name, table_name, condition, action, enabled, created_at
            FROM magiccache_alerts ORDER BY created_at DESC
        """)
        columns = ['id', 'name', 'table_name', 'condition', 'action', 'enabled', 'created_at']
        alerts = [dict(zip(columns, row)) for row in cursor.fetchall()]
        return jsonify({"alerts": alerts})

@self.app.route('/api/alerts', methods=['POST'])
def create_alert():
    data = request.json
    with self.db_connection.cursor() as cursor:
        cursor.execute("""
            INSERT INTO magiccache_alerts (name, table_name, condition, action, enabled)
            VALUES (%s, %s, %s, %s, %s) RETURNING id
        """, (data['name'], data['table'], json.dumps(data['condition']), data['action'], True))
        alert_id = cursor.fetchone()[0]
        self.db_connection.commit()
        return jsonify({"id": alert_id}), 201
```

**Step 3: Test alerts API**

```bash
curl -X POST http://localhost:8200/api/alerts \
  -H "Content-Type: application/json" \
  -d '{"name": "test", "table": "test", "condition": {}, "action": "webhook"}'
```

**Step 4: Commit**

```bash
git add daemon/src/lvc_service.py
git commit -m "feat: add alert management API"
```

---

### Task 12: Add WebSocket Real-Time Updates

**Files:**
- Modify: `daemon/src/lvc_service.py`

**Step 1: Add flask-socketio**

```python
from flask_socketio import SocketIO, emit, join_room
```

**Step 2: Set up SocketIO**

```python
def __init__(self):
    # ... existing code ...
    self.socketio = SocketIO(self.app, cors_allowed_origins="*")
    self._setup_websocket()

def _setup_websocket(self):
    @self.socketio.on('subscribe')
    def handle_subscribe(data):
        join_room(data['topic'])
        emit('subscribed', {'topic': data['topic']})

    @self.socketio.on('disconnect')
    def handle_disconnect():
        pass
```

**Step 3: Emit updates when data arrives**

In MQTT callback:

```python
def on_mqtt_message(self, client, userdata, msg):
    # ... existing persistence code ...
    topic = msg.topic
    payload = json.loads(msg.payload.decode())
    self.socketio.emit('data_update', {
        'topic': topic,
        'data': payload
    }, room=topic)
```

**Step 4: Update Flask run to use SocketIO**

```python
def run_api_server(self, port=8200):
    def _run():
        self.socketio.run(self.app, host='0.0.0.0', port=port, debug=False)
    thread = threading.Thread(target=_run, daemon=True)
    thread.start()
```

**Step 5: Commit**

```bash
git add daemon/src/lvc_service.py
git commit -m "feat: add WebSocket real-time subscriptions"
```

---

## Phase 4: MagicCache Dashboard UI

### Task 13: Create MagicCache Dashboard JavaScript Module

**Files:**
- Create: `tools/webapp/static/js/magiccache.js`

**Step 1: Create the module**

```javascript
class MagicCacheDashboard {
  constructor(apiBaseUrl = 'http://localhost:8200') {
    this.apiBase = apiBaseUrl;
    this.tables = [];
  }

  async init() {
    await this.loadTables();
    this.setupUI();
  }

  async loadTables() {
    try {
      const response = await fetch(this.apiBase + '/api/tables');
      const data = await response.json();
      this.tables = data.tables;
      this.renderTableList();
    } catch (err) {
      console.error('Failed to load tables:', err);
    }
  }

  renderTableList() {
    const container = document.getElementById('magiccache-table-list');
    if (!container) return;
    container.innerHTML = '';
    this.tables.forEach(table => {
      const div = document.createElement('div');
      div.className = 'magiccache-table-item';
      div.textContent = table.name + ' (' + table.record_count + ')';
      div.onclick = () => this.selectTable(table.name);
      container.appendChild(div);
    });
  }

  async selectTable(tableName) {
    const response = await fetch(this.apiBase + '/api/query/' + tableName + '?limit=50');
    const data = await response.json();
    this.renderRecords(data.records);
  }

  renderRecords(records) {
    const container = document.getElementById('magiccache-records');
    if (!container || records.length === 0) return;

    const table = document.createElement('table');
    table.className = 'data-table';

    const headers = Object.keys(records[0]);
    const thead = document.createElement('thead');
    const headerRow = document.createElement('tr');
    headers.forEach(h => {
      const th = document.createElement('th');
      th.textContent = h;
      headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);

    const tbody = document.createElement('tbody');
    records.forEach(record => {
      const row = document.createElement('tr');
      headers.forEach(h => {
        const td = document.createElement('td');
        td.textContent = record[h];
        row.appendChild(td);
      });
      tbody.appendChild(row);
    });
    table.appendChild(tbody);

    container.innerHTML = '';
    container.appendChild(table);
  }

  setupUI() {
    const exportBtn = document.getElementById('magiccache-export-btn');
    if (exportBtn) {
      exportBtn.onclick = () => this.exportTable();
    }
  }

  exportTable() {
    // Implementation for export
  }
}

window.MagicCacheDashboard = MagicCacheDashboard;
```

**Step 2: Test syntax**

```bash
node --check tools/webapp/static/js/magiccache.js
```

**Step 3: Commit**

```bash
git add tools/webapp/static/js/magiccache.js
git commit -m "feat: add MagicCache dashboard JavaScript module"
```

---

### Task 14: Add MagicCache Panel to Webapp

**Files:**
- Modify: `tools/webapp/static/index.html`
- Modify: `tools/webapp/static/css/style.css`

**Step 1: Add HTML panel**

In index.html, add after device panels:

```html
<div id="magiccache-panel" class="panel" style="display:none;">
  <div class="panel-header">
    <h2>Magic Cache Explorer</h2>
    <button id="magiccache-export-btn" class="btn">Export</button>
  </div>
  <div class="magiccache-container">
    <div class="magiccache-sidebar">
      <h3>Tables</h3>
      <div id="magiccache-table-list"></div>
    </div>
    <div class="magiccache-main">
      <div id="magiccache-records"></div>
    </div>
  </div>
</div>
```

**Step 2: Add CSS styling**

```css
#magiccache-panel {
  display: grid;
  grid-template-rows: auto 1fr;
  gap: 1rem;
  padding: 1rem;
}

.magiccache-container {
  display: grid;
  grid-template-columns: 250px 1fr;
  gap: 1rem;
}

.magiccache-table-item {
  padding: 0.75rem;
  margin-bottom: 0.5rem;
  background: rgba(124, 58, 237, 0.1);
  border: 1px solid var(--accent);
  border-radius: 4px;
  cursor: pointer;
}

.data-table {
  width: 100%;
  border-collapse: collapse;
}

.data-table th {
  background: rgba(124, 58, 237, 0.2);
  color: var(--accent);
  padding: 0.75rem;
  text-align: left;
}

.data-table td {
  padding: 0.75rem;
  border-bottom: 1px solid var(--border);
}
```

**Step 3: Commit**

```bash
git add tools/webapp/static/index.html tools/webapp/static/css/style.css
git commit -m "feat: add MagicCache dashboard panel UI"
```

---

### Task 15: Update Webapp to Load MagicCache Conditionally

**Files:**
- Modify: `tools/webapp/server.py`
- Modify: `tools/webapp/static/js/app.js`

**Step 1: Add config endpoint to server.py**

```python
@app.route('/api/config', methods=['GET'])
def get_config():
    magiccache_enabled = False
    try:
        response = requests.get('http://localhost:8200/api/tables', timeout=2)
        magiccache_enabled = response.status_code == 200
    except:
        pass

    return jsonify({
        "branding": get_branding(),
        "services": {
            "magiccache": {
                "enabled": magiccache_enabled,
                "api_url": "http://localhost:8200"
            }
        }
    })
```

**Step 2: Update app.js to use config**

```javascript
async function loadConfig() {
  const response = await fetch('/api/config');
  const config = await response.json();

  const branding = config.branding;
  document.documentElement.style.setProperty('--accent', branding.accent_color);

  if (config.services.magiccache.enabled) {
    const panel = document.getElementById('magiccache-panel');
    if (panel) {
      panel.style.display = 'grid';
      window.magicCacheDashboard = new MagicCacheDashboard(config.services.magiccache.api_url);
      window.magicCacheDashboard.init();
    }
  }
}

loadConfig();
```

**Step 3: Update HTML to include magiccache.js script**

In index.html, add before closing body:

```html
<script src="/static/js/magiccache.js"></script>
```

**Step 4: Commit**

```bash
git add tools/webapp/server.py tools/webapp/static/js/app.js tools/webapp/static/index.html
git commit -m "feat: webapp loads MagicCache conditionally"
```

---

## Phase 5: Testing & Documentation

### Task 16: End-to-End Testing

Manual testing (no files created).

**Steps:**
1. Stop all services
2. Start daemon — verify `[Magic]` messages
3. Verify `/api/branding` returns Magic config
4. Start webapp — verify title and colors
5. If MagicCache enabled, test dashboard
6. Test API endpoints with curl

---

### Task 17: Create Documentation

**Files:**
- Create: `docs/MAGIC_REBRAND.md`

```markdown
# Magic Rebrand — Quick Reference

## What Changed

- Daemon: Renamed to MagicDaemon, "Magic" branding
- Webapp: Updated title, octopus colors (purples/blues)
- MagicCache: New REST API endpoints + dashboard

## Quick Start

```bash
cd daemon && python src/main.py
cd tools/webapp && python server.py
```

Visit: http://localhost:8000

## APIs

- Daemon: http://localhost:8001/api/branding
- MagicCache: http://localhost:8200/api/tables

See `docs/plans/2026-03-30-magic-rebrand-magiccache-design.md` for full details.
```

---

## Summary

- **17 tasks** organized in 5 phases
- **Phase 1:** Core daemon rebrand (4 tasks)
- **Phase 2:** Webapp branding system (5 tasks)
- **Phase 3:** MagicCache REST API (3 tasks)
- **Phase 4:** Dashboard UI (3 tasks)
- **Phase 5:** Testing & docs (2 tasks)
- **Frequent commits** after each logical step
- **Total estimated time:** 4-6 hours

---

## Execution Options

**Which approach do you prefer?**

1. **Subagent-Driven (This Session)** — I dispatch fresh subagent per task, code review after each
2. **Parallel Session** — Open new session with `executing-plans`, batch execution with checkpoints

Also note: You mentioned **"logo in media folder"** — please verify if `media/magiclogo.png` exists and if you want to use it or create a new octopus logo SVG.
