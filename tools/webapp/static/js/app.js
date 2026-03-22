/**
 * app.js — LoRaLink Unified UI Engine
 * Consolidates status, mesh, and control logic into a single-pane-of-glass app.
 */

// ── State ──────────────────────────────────────────────────────────────────
let ws = null;
let lastStatus = {};
let _activeNodeId = null;
let activePage = "dashboard";
let _pendingGpio = {};
const GPIO_PENDING_TIMEOUT = 5000;
let _map = null;
let _mapMarker = null;
let _mapInitialized = false;
let _lastLat = 0, _lastLon = 0;

// ── Initialization ──────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
    wsConnect();
    loadNodes();
    loadSettings();
    loadSchedule();
    setInterval(loadSchedule, 5000);
});

// ── Navigation ──────────────────────────────────────────────────────────────
function switchPage(name) {
    activePage = name;
    document.querySelectorAll(".page").forEach(p => p.classList.remove("active"));
    document.querySelectorAll(".nav-item").forEach(l => l.classList.remove("active"));
    
    const pg = document.getElementById("pg-" + name);
    if (pg) pg.classList.add("active");
    
    // Highlight sidebar nav item
    const navItems = document.querySelectorAll(".nav-item");
    navItems.forEach(item => {
        if (item.getAttribute('onclick').includes(`'${name}'`)) {
            item.classList.add("active");
        }
    });

    if (name === "map" && !_mapInitialized) {
        setTimeout(initMap, 100);
    }
    if (name === "tools") {
        loadFiles();
        loadSchedule();
    }
}

// ── WebSocket ─────────────────────────────────────────────────────────────
function wsConnect() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${proto}://${location.host}/ws`);

    ws.onmessage = (e) => {
        const data = JSON.parse(e.data);
        if (data.type === 'status') {
            onStatusUpdate(data);
        } else if (data.type === 'log' || data.type === 'serial_log') {
            logTerminal(data.msg || data.text || data.line, data.type === 'serial_log' ? 'rx' : 'info');
        } else if (data.type === 'ai_status') {
            updateAiUI(data);
        }
    };

    ws.onopen = () => {
        document.getElementById("conn-status").textContent = "CONNECTED";
        document.getElementById("conn-status").style.color = "var(--ok)";
    };

    ws.onclose = () => {
        document.getElementById("conn-status").textContent = "OFFLINE";
        document.getElementById("conn-status").style.color = "var(--err)";
        setTimeout(wsConnect, 2500);
    };
}

// ── Status Updates ─────────────────────────────────────────────────────────
function onStatusUpdate(d) {
    lastStatus = d;
    renderDashboard(d);
    
    // Update active node state if backend switched
    if (d.id && d.id !== _activeNodeId) {
        _activeNodeId = d.id;
        // Optionally reload nodes to update sidebar active class
    }

    if (d.lat && d.lon) updateMapUI(d);
}

function renderDashboard(d) {
    // Header
    const nameEl = document.getElementById("active-node-name");
    const verEl = document.getElementById("fw-ver");
    if (nameEl) nameEl.textContent = `Node: ${d.active_node || d.id || "Gateway"}`;
    if (verEl) verEl.textContent = d.ver || d.version || "";

    // Telemetry Cards
    const bat = parseFloat(d.bat) || 0;
    const rssi = parseInt(d.rssi) || 0;
    const health = computeHealthScore(d);
    
    setValText("val-bat", bat ? `${bat.toFixed(2)}V` : "—", bat > 3.4 ? "ok" : "warn");
    setValText("val-rssi", rssi ? `${rssi} dBm` : "—", rssi > -95 ? "" : "warn");
    setValText("val-uptime", d.uptime || "—");
    setValText("val-reset", d.reset || "—");
    setValText("val-health", `${health.score}%`, health.cls);

    // GPIO States
    const pins = d.pins || [];
    pins.forEach(p => {
        const btn = document.getElementById(`btn-${p.n}`) || document.getElementById(`btn-${p.p}`);
        if (btn) {
            const hi = Boolean(parseInt(p.v));
            btn.classList.toggle("on", hi);
        }
    });

    // Mesh
    renderMeshTable(d.mesh || []);
}

function computeHealthScore(d) {
    let score = 100;
    const bat = parseFloat(d.bat) || 0;
    const rssi = parseInt(d.rssi) || -120;
    
    if (bat > 0 && bat < 3.3) score -= 30;
    if (rssi < -105) score -= 20;
    if (d.heap && d.heap < 50000) score -= 15;
    
    const cls = score > 80 ? "ok" : (score > 50 ? "warn" : "err");
    return { score: Math.max(score, 0), cls };
}

function setValText(id, val, cls) {
    const el = document.getElementById(id);
    if (el) {
        el.textContent = val;
        if (cls) el.className = `val ${cls}`;
    }
}

// ── Node Management ────────────────────────────────────────────────────────
async function loadNodes() {
    try {
        const r = await fetch("/api/nodes");
        const res = await r.json();
        const nodes = res.nodes || [];
        const sideList = document.getElementById("sidebar-node-list");
        
        if (sideList) {
            sideList.innerHTML = nodes.map(n => `
                <div class="node-pill ${n.active ? 'active' : ''}" onclick="connectToNode('${n.id}')">
                    <span class="status-dot" style="background: ${n.online ? 'var(--ok)' : '#555'}"></span>
                    <span style="flex:1; font-size:12px; font-weight:600">${escHtml(n.name)}</span>
                    <span style="font-size:9px; opacity:0.5; font-family:monospace">${n.type}</span>
                </div>
            `).join("");
        }
    } catch (e) {
        console.error("loadNodes fail", e);
    }
}

async function connectToNode(id) {
    try {
        const r = await fetch(`/api/nodes/${id}/connect`, { method: "POST" });
        const d = await r.json();
        if (d.ok) {
            _activeNodeId = id;
            logTerminal(`Switching to node: ${d.node.name}`, 'info');
            loadNodes();
        }
    } catch (e) {
        logTerminal(`Connect failed: ${e.message}`, 'err');
    }
}

function updateSidebarSelection(activeId) {
    // Optional: refine sidebar highlighting if ID matches
}

// ── Control ────────────────────────────────────────────────────────────────
async function togglePin(name, pinNum) {
    const btn = document.getElementById(`btn-${name}`) || document.getElementById(`btn-${pinNum}`);
    const isCurrentlyOn = btn && btn.classList.contains("on");
    const newVal = isCurrentlyOn ? "0" : "1";
    
    logTerminal(`SET GPIO ${name} -> ${newVal}`, 'tx');
    await sendTerminalCmd(`GPIO ${name} ${newVal}`);
}

async function sendTerminalCmd(cmd) {
    if (!cmd) return;
    try {
        await fetch("/api/cmd", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ cmd, node_id: _activeNodeId }),
        });
    } catch (e) {
        logTerminal(`CMD Error: ${e.message}`, 'err');
    }
}

function handleTerminalKey(ev) {
    if (ev.key === "Enter") {
        const inp = document.getElementById("cmd-input");
        const cmd = inp.value.trim();
        if (cmd) {
            logTerminal(cmd, 'tx');
            sendTerminalCmd(cmd);
            inp.value = "";
        }
    }
}

function logTerminal(msg, type = 'info') {
    const log = document.getElementById("terminal-log");
    if (!log) return;
    const ts = new Date().toLocaleTimeString();
    const div = document.createElement("div");
    div.className = "log-entry";
    
    let pfx = "::";
    let cls = "src-rx";
    if (type === 'tx') { pfx = ">>"; cls = "src-tx"; }
    else if (type === 'err') { pfx = "!!"; cls = "src-err"; }
    
    div.innerHTML = `<span class="ts">${ts}</span><span class="${cls}">${pfx}</span> ${escHtml(msg)}`;
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
    while (log.children.length > 100) log.removeChild(log.firstChild);
}

// ── Mesh Table ─────────────────────────────────────────────────────────────
function renderMeshTable(nodes) {
    const tb = document.getElementById("mesh-tbody");
    if (!tb) return;
    if (!nodes.length) {
        tb.innerHTML = '<tr><td colspan="5" class="text-center text-dim p-4">Empty Swarm</td></tr>';
        return;
    }
    tb.innerHTML = nodes.map(n => `
        <tr>
            <td class="text-bold text-accent">${n.id}</td>
            <td style="color:${n.bat > 3.5 ? 'var(--ok)' : 'var(--warn)'}">${n.bat ? n.bat.toFixed(2) + "V" : "—"}</td>
            <td>${n.rssi || "—"} dBm</td>
            <td>${n.hops || "0"}</td>
            <td class="text-dim">${n.ago ? n.ago + "s" : "now"}</td>
        </tr>
    `).join("");
}

function updateAiUI(d) {
    const statusEl = document.getElementById("ai-status");
    const promptEl = document.getElementById("ai-prompt");
    const respEl = document.getElementById("ai-response");
    
    if (statusEl) {
        statusEl.textContent = d.status?.toUpperCase() || "IDLE";
        statusEl.className = `val ${d.status === 'querying' ? 'warn' : 'ok'}`;
    }
    if (d.prompt && promptEl) promptEl.textContent = d.prompt;
    if (d.response && respEl) respEl.textContent = d.response;
}
function initMap() {
    if (_mapInitialized) return;
    const el = document.getElementById('leaflet-map');
    if (!el) return;
    
    _map = L.map('leaflet-map').setView([0, 0], 2);
    L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
        attribution: '&copy; CARTO'
    }).addTo(_map);
    _mapInitialized = true;
}

function updateMapUI(d) {
    if (!_mapInitialized) return;
    const lat = parseFloat(d.lat), lon = parseFloat(d.lon);
    if (!lat || !lon) return;
    _lastLat = lat; _lastLon = lon;
    
    if (!_mapMarker) {
        _mapMarker = L.marker([lat, lon]).addTo(_map);
        _map.setView([lat, lon], 15);
    } else {
        _mapMarker.setLatLng([lat, lon]);
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────
function escHtml(s) {
    if (!s) return "";
    return s.toString().replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

async function loadSettings() {
    try {
        const r = await fetch("/api/settings");
        const d = await r.json();
        const sel = document.getElementById("xport-sel");
        if (sel && d.transport_strategy) sel.value = d.transport_strategy;
    } catch (e) {}
}

async function setTransport(strategy) {
    await fetch("/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ transport_strategy: strategy }),
    });
}

function openTransportModal() { document.getElementById("transport-modal").classList.add("open"); }
function closeModal() { document.getElementById("transport-modal").classList.remove("open"); }

// ── Tools / Sched ─────────────────────────────────────────────────────────
async function loadSchedule() {
    try {
        const r = await fetch("/api/schedule");
        const d = await r.json();
        const tb = document.getElementById("sched-tbody");
        if (tb && d.schedules) {
            tb.innerHTML = d.schedules.map(t => `
                <tr>
                    <td class="text-bold">${t.name}</td>
                    <td><span class="badge ${t.enabled ? 'on' : 'off'}">${t.type}</span></td>
                    <td class="text-dim font-mono">p${t.pin}</td>
                    <td>${Math.round(t.interval/1000)}s</td>
                </tr>
            `).join("");
        }
    } catch (e) {}
}

async function loadFiles() {
    const r = await fetch("/api/files");
    const d = await r.json();
    const el = document.getElementById("file-list");
    if (el && d.files) {
        el.innerHTML = d.files.map(f => `
            <div class="p-2 border-b border-dim text-xs font-mono">${f.name} (${f.size}B)</div>
        `).join("");
    }
}
