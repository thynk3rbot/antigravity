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
let _daemonNodes = [];      // Nodes known to daemon
let _daemonConnected = false;
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
    if (name === "plugins") {
        refreshPlugins();
    }
    if (name === "hybrid-proxy") {
        loadProxyStatus();
        setInterval(loadProxyStatus, 5000);
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
        } else if (data.type === 'daemon_nodes') {
            _daemonNodes = data.daemon_nodes || [];
            _daemonConnected = Array.isArray(data.daemon_nodes);
            renderDaemonSwarm(_daemonNodes);
            updateDaemonStatus(_daemonConnected, _daemonNodes.length);
        } else if (data.type === 'log' || data.type === 'serial_log') {
            logTerminal(data.msg || data.text || data.line, data.type === 'serial_log' ? 'rx' : 'info', data.source);
            if (typeof handleTransformMessage === 'function') handleTransformMessage(data);
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
let _pendingStatus = null;
function onStatusUpdate(d) {
    _pendingStatus = d;
    // Throttle rendering to animation frames to prevent DOM thrashing
    requestAnimationFrame(() => {
        if (!_pendingStatus) return;
        const data = _pendingStatus;
        _pendingStatus = null;
        
        lastStatus = data;
        renderDashboard(data);
        if (data.detected_devices) renderPresenceTable(data.detected_devices);
        if (data.lat && data.lon) updateMapUI(data);
    });
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

    // Parse optional target prefix: "node-alpha GPIO 5 HIGH" or "ALL RELAY 1 ON"
    let target = _activeNodeId;
    let actualCmd = cmd.trim();

    const parts = actualCmd.split(/\s+/);
    if (parts.length > 1) {
        const potentialTarget = parts[0];
        const knownIds = _daemonNodes.map(n => n.id);
        if (potentialTarget === 'ALL' || knownIds.includes(potentialTarget)) {
            target = potentialTarget;
            actualCmd = parts.slice(1).join(' ');
        }
    }

    if (target === 'ALL') {
        // Fan out to all daemon nodes in parallel
        const targets = _daemonNodes.length ? _daemonNodes : [];
        if (!targets.length) {
            logTerminal('[ALL] No daemon nodes registered', 'warn');
            return;
        }
        logTerminal(`[ALL → ${targets.length} nodes] ${actualCmd}`, 'tx');
        await Promise.all(targets.map(n =>
            fetch("/api/cmd", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ cmd: actualCmd, node_id: n.id })
            }).then(r => r.text()).then(result => {
                logTerminal(`  [${n.id}] ← ${result}`, result === 'OK' ? 'rx' : 'err');
            }).catch(e => {
                logTerminal(`  [${n.id}] ✗ ${e.message}`, 'err');
            })
        ));
    } else {
        try {
            const r = await fetch("/api/cmd", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ cmd: actualCmd, node_id: target }),
            });
            const text = await r.text();
            if (!r.ok) logTerminal(`CMD Error: ${text}`, 'err');
        } catch (e) {
            logTerminal(`CMD Error: ${e.message}`, 'err');
        }
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

function logTerminal(msg, type = 'info', source = null) {
    const log = document.getElementById("terminal-log");
    if (!log) return;
    const ts = new Date().toLocaleTimeString();
    const div = document.createElement("div");
    div.className = "log-entry";
    
    let pfx = "::";
    let cls = "src-rx";
    if (type === 'tx') { pfx = ">>"; cls = "src-tx"; }
    else if (type === 'err') { pfx = "!!"; cls = "src-err"; }
    
    let srcHtml = source ? `<span style="color:var(--text-dim); margin-right:4px;">[${escHtml(source)}]</span>` : "";
    
    div.innerHTML = `<span class="ts">${ts}</span> ${srcHtml} <span class="${cls}">${pfx}</span> ${escHtml(msg)}`;
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
    while (log.children.length > 200) log.removeChild(log.firstChild);
}

// ── Mesh Table ─────────────────────────────────────────────────────────────
function renderMeshTable(nodes) {
    const tb = document.getElementById("mesh-tbody");
    if (!tb) return;
    if (!nodes.length) {
        tb.innerHTML = '<tr><td colspan="6" class="text-center text-dim p-4">Empty Swarm</td></tr>';
        return;
    }
    tb.innerHTML = nodes.map(n => {
        const id = n.id || n.nodeId || "Unknown";
        const bat = parseFloat(n.bat) || 0;
        const rssi = parseInt(n.rssi) || 0;
        const hops = parseInt(n.hops) || 0;
        const ago = n.ago !== undefined ? n.ago : (n.last_seen !== undefined ? n.last_seen : "—");

        return `
            <tr>
                <td class="text-bold text-accent">${escHtml(id)}</td>
                <td style="color:${bat > 3.5 ? 'var(--ok)' : (bat > 3.1 ? 'var(--warn)' : 'var(--err)')}">${bat ? bat.toFixed(2) + "V" : "—"}</td>
                <td>${rssi ? rssi + " dBm" : "—"}</td>
                <td>${hops}</td>
                <td class="text-dim">${ago === "now" ? "now" : (typeof ago === 'number' ? ago + "s" : ago)}</td>
            </tr>
        `;
    }).join("");
}

// ── Daemon Swarm Management ────────────────────────────────────────────────
function renderDaemonSwarm(nodes) {
    const tb = document.getElementById("mesh-tbody");
    if (!tb) return;

    // Merge: daemon nodes take priority, add mesh nodes not already listed
    const allNodes = [...nodes];
    (lastStatus.mesh || []).forEach(m => {
        if (!allNodes.find(n => n.id === m.id || n.id === m.nodeId)) {
            allNodes.push({ ...m, _source: 'mesh' });
        }
    });

    if (!allNodes.length) {
        tb.innerHTML = '<tr><td colspan="6" class="text-center text-dim p-4">No nodes in swarm — waiting for autodiscovery</td></tr>';
        return;
    }

    tb.innerHTML = allNodes.map(n => {
        const id = n.id || n.nodeId || "?";
        const name = n.name || id;
        const bat = parseFloat(n.bat) || 0;
        const rssi = parseInt(n.rssi) || 0;
        const hops = parseInt(n.hops) || 0;
        const online = n.online !== false;
        const isActive = _activeNodeId === id;
        const isDaemon = !n._source;
        const ago = n.last_seen ? Math.round((Date.now()/1000) - n.last_seen) + "s" : (n.ago || "—");

        return `<tr class="${isActive ? 'node-active-row' : ''}" onclick="selectDaemonNode('${escHtml(id)}')" style="cursor:pointer">
            <td>
                <span class="status-dot" style="background:${online ? 'var(--ok)' : '#555'}; display:inline-block; width:7px; height:7px; border-radius:50%; margin-right:6px;"></span>
                <span class="text-bold text-accent">${escHtml(name)}</span>
                ${isDaemon ? '<span style="font-size:9px;color:var(--muted);margin-left:4px;">daemon</span>' : ''}
                ${isActive ? '<span style="font-size:9px;color:var(--ok);margin-left:4px;font-weight:700;">● ACTIVE</span>' : ''}
            </td>
            <td style="color:${bat > 3.5 ? 'var(--ok)' : (bat > 3.1 ? 'var(--warn)' : (bat ? 'var(--err)' : 'var(--muted)'))}">
                ${bat ? bat.toFixed(2) + "V" : "—"}
            </td>
            <td>${rssi ? rssi + " dBm" : "—"}</td>
            <td>${hops || "—"}</td>
            <td class="text-dim">${ago}</td>
            <td>
                <button class="btn sm" onclick="event.stopPropagation(); quickCmd('${escHtml(id)}', 'STATUS')" style="padding:2px 8px;font-size:10px;">STATUS</button>
            </td>
        </tr>`;
    }).join("");
}

function selectDaemonNode(nodeId) {
    _activeNodeId = nodeId;
    // Re-render to show ACTIVE badge
    renderDaemonSwarm(_daemonNodes);
    logTerminal(`Active node → ${nodeId}`, 'info');
    // Update header if element exists
    const nameEl = document.getElementById("active-node-name");
    if (nameEl) nameEl.textContent = `Node: ${nodeId}`;
}

async function quickCmd(nodeId, cmd) {
    logTerminal(`[${nodeId}] → ${cmd}`, 'tx');
    try {
        const r = await fetch("/api/cmd", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ cmd, node_id: nodeId })
        });
        const text = await r.text();
        logTerminal(`[${nodeId}] ← ${text}`, r.ok ? 'rx' : 'err');
    } catch (e) {
        logTerminal(`[${nodeId}] CMD error: ${e.message}`, 'err');
    }
}

function updateDaemonStatus(connected, nodeCount) {
    const el = document.getElementById("daemon-status");
    if (!el) return;
    if (connected) {
        el.textContent = `⬡ DAEMON  ${nodeCount} node${nodeCount !== 1 ? 's' : ''}`;
        el.style.color = "var(--ok)";
    } else {
        el.textContent = "⬡ DAEMON  offline";
        el.style.color = "var(--err)";
    }
}

// ── Presence (Marauder) ───────────────────────────────────────────────────
function renderPresenceTable(devices) {
    const tb = document.getElementById("presence-tbody");
    if (!tb) return;
    if (!devices.length) {
        tb.innerHTML = '<tr><td colspan="5" class="text-center text-dim p-8">No devices detected in current scan.</td></tr>';
        return;
    }
    tb.innerHTML = devices.map(d => {
        const ago = d.lastSeen ? Math.round((Date.now() - d.lastSeen) / 1000) : "—";
        return `
            <tr>
                <td class="font-mono text-accent">${escHtml(d.mac)}</td>
                <td><span class="badge ${d.type === 'AP' ? 'ok' : 'secondary'}">${d.type}</span></td>
                <td>${escHtml(d.ssid || d.details || "—")}</td>
                <td style="color:${d.rssi > -70 ? 'var(--ok)' : (d.rssi > -90 ? 'var(--warn)' : 'var(--err)')}">${d.rssi} dBm</td>
                <td class="text-dim">${ago}s ago</td>
            </tr>
        `;
    }).join("");
}

async function toggleSniffer() {
    const btn = document.getElementById("btn-sniff-toggle");
    const currentlyOn = btn.textContent.includes("Stop");
    const enable = !currentlyOn;
    
    try {
        const r = await fetch("/api/probe/sniff", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: `enabled=${enable}`
        });
        if (r.ok) {
            btn.textContent = enable ? "Stop Sniffer" : "Start Sniffer";
            btn.classList.toggle("danger", enable);
            logTerminal(`WiFi Sniffer ${enable ? 'ENABLED' : 'DISABLED'}`, 'info');
        }
    } catch (e) {
        logTerminal(`Sniffer toggle failed: ${e.message}`, 'err');
    }
}

async function toggleHopping() {
    const btn = document.getElementById("btn-hop-toggle");
    const currentlyOn = btn.classList.contains("ok");
    const enable = !currentlyOn;

    try {
        const r = await fetch("/api/probe/hop", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: `enabled=${enable}`
        });
        if (r.ok) {
            btn.classList.toggle("ok", enable);
            btn.classList.toggle("secondary", !enable);
            logTerminal(`Channel Hopping ${enable ? 'ENABLED' : 'DISABLED'}`, 'info');
        }
    } catch (e) {
        logTerminal(`Hopping toggle failed: ${e.message}`, 'err');
    }
}

async function clearSnifferRegistry() {
    // Note: No backend endpoint for clearRegistry yet, but we can clear local UI
    // and wait for next telemetry.
    const tb = document.getElementById("presence-tbody");
    if (tb) tb.innerHTML = '<tr><td colspan="5" class="text-center text-dim p-8">Registry cleared.</td></tr>';
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

// ── Plugin Registry ───────────────────────────────────────────────────────
async function refreshPlugins() {
    const grid = document.getElementById("plugin-grid");
    if (!grid) return;
    grid.innerHTML = '<div class="text-center text-dim p-8 col-span-full italic">Scanning node for products...</div>';
    
    try {
        const r = await fetch("/api/files?prefix=/products/");
        const d = await r.json();
        const files = (d.files || []).filter(f => f.name.endsWith(".json"));
        
        if (!files.length) {
            grid.innerHTML = '<div class="text-center text-dim p-8 col-span-full">No product profiles found in /products/</div>';
            return;
        }

        grid.innerHTML = files.map(f => {
            const name = f.name.replace(".json", "");
            return `
                <div class="card p-4 hover-lift">
                    <div class="flex-row justify-between mb-2">
                        <span class="text-accent font-bold uppercase text-xs">${escHtml(name)}</span>
                        <span class="text-xxs text-dim">${f.size}B</span>
                    </div>
                    <div class="text-dim text-xs mb-4">Industrial hardware personality for ${escHtml(name)}.</div>
                    <div class="flex-row gap-2">
                        <button class="btn sm flex-1" onclick="applyPlugin('${escHtml(name)}')">Apply</button>
                        <button class="btn sm secondary" onclick="editPlugin('${escHtml(name)}')"><i class="fas fa-edit"></i></button>
                    </div>
                </div>
            `;
        }).join("");
    } catch (e) {
        grid.innerHTML = `<div class="text-center text-err p-8 col-span-full">Failed to load registry: ${e.message}</div>`;
    }
}

async function uploadPlugin() {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".json";
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        
        const path = `/products/${file.name}`;
        logTerminal(`Uploading ${file.name} to /products/...`, 'tx');
        
        const fd = new FormData();
        fd.append("file", file);
        fd.append("path", path);

        try {
            const r = await fetch("/api/upload", { method: "POST", body: fd });
            if (r.ok) {
                logTerminal(`Upload SUCCESS: ${file.name}`, 'ok');
                refreshPlugins();
            } else {
                throw new Error(await r.text());
            }
        } catch (err) {
            logTerminal(`Upload FAIL: ${err.message}`, 'err');
        }
    };
    input.click();
}

async function applyPlugin(name) {
    if (!confirm(`Apply [${name}] personality? This will reconfigure hardware pins.`)) return;
    logTerminal(`Applying hardware personality: ${name}`, 'tx');
    await sendTerminalCmd(`LOAD /products/${name}.json`);
    setTimeout(() => {
        logTerminal(`Plugin [${name}] applied.`, 'ok');
        location.reload(); // Refresh to update telemetry with new pins
    }, 1500);
}

// ── Product Configurator ───────────────────────────────────────────────────
async function editPlugin(name) {
    try {
        const r = await fetch(`/api/files/read?path=/products/${name}.json`);
        const d = await r.json();
        
        document.getElementById("product-filename").textContent = `/products/${name}.json`;
        document.getElementById("product-json-editor").value = JSON.stringify(d, null, 4);
        document.getElementById("product-modal").classList.add("open");
    } catch (e) {
        logTerminal("Failed to load manifest: " + e.message, 'err');
    }
}

function closeProductModal() {
    document.getElementById("product-modal").classList.remove("open");
}

async function saveProductManifest() {
    const filename = document.getElementById("product-filename").textContent;
    const json = document.getElementById("product-json-editor").value;
    
    try {
        JSON.parse(json); // Validate
    } catch (e) {
        alert("Invalid JSON: " + e.message);
        return;
    }

    const fd = new FormData();
    fd.append("file", new Blob([json], { type: 'application/json' }), filename.split('/').pop());
    fd.append("path", filename);

    logTerminal(`Saving manifest to ${filename}...`, 'tx');
    try {
        const r = await fetch("/api/upload", { method: "POST", body: fd });
        if (r.ok) {
            logTerminal("Manifest saved. Rebooting node...", 'ok');
            await sendTerminalCmd("REBOOT");
            closeProductModal();
        } else {
            throw new Error(await r.text());
        }
    } catch (e) {
        logTerminal("Save failed: " + e.message, 'err');
    }
}

function sendDose(pump, ml) {
    const cmd = `NUTRIC nutricalc/pump/${pump}|{"ml":${ml}}`;
    logTerminal(`Dosing Pump ${pump}: ${ml}ml`, 'tx');
    sendTerminalCmd(cmd);
}

// ── Auto-Wiring (Plugin UI Generation) ─────────────────────────────────────
async function handleAutoWiring(status) {
    const product = status.active_product;
    if (!product) return;

    const container = document.getElementById("plugin-controls-root");
    if (!container) return;

    if (container.getAttribute('data-active-plugin') === product) {
        updatePluginStates(status);
        return;
    }

    try {
        const resp = await fetch(`/static/templates/${product}.json`);
        if (!resp.ok) throw new Error("Template not found");
        const template = await resp.json();
        
        container.setAttribute('data-active-plugin', product);
        renderPluginUI(container, template);
        updatePluginStates(status);
    } catch (e) {
        console.warn("[Auto-Wiring] Template load failed: " + product);
    }
}

function renderPluginUI(container, template) {
    container.innerHTML = `
        <div class="panel">
            <div class="panel-title"><i class="${template.icon}"></i> ${template.displayName}</div>
            <div class="p-4" id="plugin-widgets" style="display:grid; grid-template-columns:1fr 1fr; gap:10px;"></div>
        </div>
    `;
    
    const grid = document.getElementById("plugin-widgets");
    template.widgets.forEach(w => {
        const div = document.createElement("div");
        div.className = "card p-3";
        if (w.type === 'toggle') {
            div.innerHTML = `
                <div class="flex-row-sb">
                    <span class="text-xs font-bold">${w.label}</span>
                    <button class="btn btn-xs" id="plug-p${w.pin}" onclick="sendPluginCmd('${w.cmd}', !this.classList.contains('ok'))">TOGGLE</button>
                </div>
            `;
        } else if (w.type === 'indicator') {
            div.innerHTML = `
                <div class="flex-row-sb">
                    <span class="text-xs">${w.label}</span>
                    <div class="status-dot" id="plug-p${w.pin}" style="background:#555"></div>
                </div>
            `;
        } else if (w.type === 'dose') {
            div.innerHTML = `
                <div class="flex-row-sb">
                    <span class="text-xs font-bold">${w.label}</span>
                    <div class="flex-row gap-2">
                        <input type="number" id="dose-val-${w.pump}" value="5" class="input-styled sm text-xs" style="width:40px; height:24px; padding:0 4px">
                        <button class="btn btn-xs" onclick="sendDose(${w.pump}, document.getElementById('dose-val-${w.pump}').value)">DOSE</button>
                    </div>
                </div>
            `;
        }
        grid.appendChild(div);
    });
}

function updatePluginStates(status) {
    const pins = status.pins || [];
    pins.forEach(p => {
        const el = document.getElementById(`plug-p${p.n || p.p}`);
        if (!el) return;
        
        const hi = Boolean(parseInt(p.v));
        if (el.tagName === 'BUTTON') {
            el.classList.toggle("ok", hi);
            el.classList.toggle("secondary", !hi);
        } else {
            el.style.background = hi ? 'var(--ok)' : '#555';
            el.style.boxShadow = hi ? '0 0 8px var(--ok)' : 'none';
        }
    });
}

function sendPluginCmd(template, val) {
    const cmd = template.replace('{val}', val ? '1' : '0');
    sendTerminalCmd(cmd);
}

// ── Visual Builder Logic ───────────────────────────────────────────────────
let builderState = { step: 1, metadata: null, selections: { board: '', plugins: [], config: {} } };

async function startVisualBuilder() {
    try {
        const r = await fetch('/static/plugins_metadata.json');
        builderState.metadata = await r.json();
        builderState.step = 1;
        renderBuilderStep();
        document.getElementById("builder-modal").classList.add("open");
    } catch (e) {
        logTerminal("Failed to load builder metadata: " + e.message, 'err');
    }
}

function closeBuilderModal() {
    document.getElementById("builder-modal").classList.remove("open");
}

function renderBuilderStep() {
    const container = document.getElementById("builder-steps-container");
    const nextBtn = document.getElementById("btn-builder-next");
    
    if (builderState.step === 1) {
        nextBtn.textContent = "Next (Select Plugins)";
        container.innerHTML = `
            <p class="text-xs text-dim mb-4">Step 1: Select your hardware base board.</p>
            <div class="grid grid-cols-1 gap-2">
                ${builderState.metadata.boards.map(b => `
                    <div class="node-pill ${builderState.selections.board === b.id ? 'active' : ''}" onclick="selectBoard('${b.id}')">
                        <i class="${b.icon}"></i>
                        <span class="flex-1">${b.name}</span>
                    </div>
                `).join('')}
            </div>
        `;
    } else if (builderState.step === 2) {
        nextBtn.textContent = "Next (Configure)";
        container.innerHTML = `
            <p class="text-xs text-dim mb-4">Step 2: select plugins to include.</p>
            <div class="grid grid-cols-1 gap-2">
                ${builderState.metadata.plugins.map(p => `
                    <div class="node-pill ${builderState.selections.plugins.includes(p.id) ? 'active' : ''}" onclick="togglePluginSelection('${p.id}')">
                        <i class="fas fa-plug"></i>
                        <span class="flex-1">${p.name}</span>
                        <input type="checkbox" ${builderState.selections.plugins.includes(p.id) ? 'checked' : ''} style="pointer-events:none">
                    </div>
                `).join('')}
            </div>
        `;
    } else if (builderState.step === 3) {
        nextBtn.textContent = "DEPLOY PERSONALITY";
        container.innerHTML = `
            <p class="text-xs text-dim mb-4">Step 3: Configure plugin parameters.</p>
            <div class="flex-col gap-4">
                ${builderState.selections.plugins.map(pluginId => {
                    const p = builderState.metadata.plugins.find(x => x.id === pluginId);
                    return `
                        <div class="panel p-3">
                            <div class="text-xs text-accent mb-2 uppercase font-bold">${p.id}</div>
                            ${p.config.map(c => `
                                <div class="mb-2">
                                    <label class="text-xs text-dim block mb-1">${c.label}</label>
                                    <input type="text" class="input-styled w-full text-xs" 
                                        id="cfg-${p.id}-${c.key}" value="${builderState.selections.config[p.id]?.[c.key] || c.default}">
                                </div>
                            `).join('')}
                        </div>
                    `;
                }).join('')}
                <div class="mb-2">
                    <label class="text-xs text-dim block mb-1">Product Name (Target Filename)</label>
                    <input type="text" class="input-styled w-full text-xs" id="cfg-filename" value="CustomDevice">
                </div>
            </div>
        `;
    }
}

function selectBoard(id) {
    builderState.selections.board = id;
    renderBuilderStep();
}

function togglePluginSelection(id) {
    const idx = builderState.selections.plugins.indexOf(id);
    if (idx > -1) builderState.selections.plugins.splice(idx, 1);
    else builderState.selections.plugins.push(id);
    renderBuilderStep();
}

async function builderNextStep() {
    if (builderState.step === 1) {
        if (!builderState.selections.board) { alert("Please select a board"); return; }
        builderState.step = 2;
    } else if (builderState.step === 2) {
        builderState.step = 3;
    } else if (builderState.step === 3) {
        await finishBuilder();
        return;
    }
    renderBuilderStep();
}

async function finishBuilder() {
    const manifest = {
        id: document.getElementById("cfg-filename").value || "CustomDevice",
        board: builderState.selections.board,
        plugins: []
    };

    builderState.selections.plugins.forEach(pluginId => {
        const p = builderState.metadata.plugins.find(x => x.id === pluginId);
        const pluginCfg = { type: pluginId };
        p.config.forEach(c => {
            const val = document.getElementById(`cfg-${pluginId}-${c.key}`).value;
            // Try to parse CSV into array if default was CSV
            if (c.default.includes(',')) {
                pluginCfg[c.key] = val.split(',').map(v => parseInt(v.trim()));
            } else {
                pluginCfg[c.key] = val;
            }
        });
        manifest.plugins.push(pluginCfg);
    });

    // Switch to raw editor to show what we built before saving
    closeBuilderModal();
    
    // Show a Wiring Summary before opening the JSON editor
    let wiringSummary = `PROVISIONING SUMMARY for ${manifest.id}:\n\n`;
    manifest.plugins.forEach(p => {
        wiringSummary += `Plugin: ${p.type}\n`;
        for (const [key, val] of Object.entries(p)) {
            if (key !== 'type') wiringSummary += `  - ${key}: ${val}\n`;
        }
    });
    
    alert(wiringSummary + "\nOpening JSON Editor for final review.");

    document.getElementById("product-filename").textContent = `/products/${manifest.id}.json`;
    document.getElementById("product-json-editor").value = JSON.stringify(manifest, null, 4);
    document.getElementById("product-modal").classList.add("open");
}

// ── Hybrid Model Proxy ─────────────────────────────────────────────────────────
async function loadProxyStatus() {
    try {
        const res = await fetch('/api/proxy/status');
        if (!res.ok) return;
        const data = await res.json();

        // Update status
        const statusEl = document.getElementById('proxy-status');
        if (statusEl) {
            statusEl.textContent = data.running ? '✓ RUNNING' : '⏸ STOPPED';
            statusEl.style.color = data.running ? 'var(--ok)' : 'var(--muted)';
        }

        // Update health
        document.getElementById('proxy-ollama-health').textContent = data.ollama_healthy ? '✓ Healthy' : '✗ Down';
        document.getElementById('proxy-openrouter-health').textContent = data.openrouter_healthy ? '✓ Healthy' : '✗ Down';
        document.getElementById('proxy-uptime').textContent = data.uptime || '—';

        // Update metrics
        document.getElementById('proxy-total-requests').textContent = data.total_requests || 0;
        document.getElementById('proxy-ollama-requests').textContent = data.ollama_requests || 0;
        document.getElementById('proxy-ollama-cost').textContent = `$` + (data.ollama_cost || 0).toFixed(4);
        document.getElementById('proxy-openrouter-requests').textContent = data.openrouter_requests || 0;
        document.getElementById('proxy-openrouter-cost').textContent = `$` + (data.openrouter_cost || 0).toFixed(4);
        document.getElementById('proxy-total-cost').textContent = `$` + (data.total_cost || 0).toFixed(4);

        // Update buttons
        document.getElementById('proxy-start-btn').style.display = data.running ? 'none' : 'inline-block';
        document.getElementById('proxy-stop-btn').style.display = data.running ? 'inline-block' : 'none';

        // Update recent requests table
        const tbody = document.getElementById('proxy-requests-tbody');
        if (tbody && data.recent_requests) {
            if (data.recent_requests.length === 0) {
                tbody.innerHTML = '<tr><td colspan="6" class="text-center text-dim p-4">No requests yet</td></tr>';
            } else {
                tbody.innerHTML = data.recent_requests.map(req => {
                    const time = new Date(req.timestamp).toLocaleTimeString();
                    const backend = req.backend;
                    const model = String(req.model).substring(0, 20);
                    const tokens = req.total_tokens;
                    const latency = (req.latency_ms || 0).toFixed(0);
                    const cost = (req.cost_usd || 0).toFixed(4);
                    return '<tr><td class="text-xs text-dim">' + time + '</td><td><span class="badge' + (backend === 'ollama' ? ' on' : '') + '">' + backend + '</span></td><td class="text-xs">' + model + '</td><td class="text-xs">' + tokens + '</td><td class="text-xs">' + latency + 'ms</td><td class="text-xs">$' + cost + '</td></tr>';
                }).join('');
            }
        }
    } catch (err) {
        console.error('Failed to load proxy status:', err);
    }
}

async function proxyControl(action) {
    try {
        if (action === 'start') {
            const res = await fetch('/api/proxy/start', { method: 'POST' });
            if (res.ok) {
                logTerminal('✓ Proxy started', 'info');
                await loadProxyStatus();
            }
        } else if (action === 'stop') {
            const res = await fetch('/api/proxy/stop', { method: 'POST' });
            if (res.ok) {
                logTerminal('✓ Proxy stopped', 'info');
                await loadProxyStatus();
            }
        } else if (action === 'test-openrouter') {
            const res = await fetch('/api/proxy/test-openrouter', { method: 'POST' });
            const data = await res.json();
            if (data.success) {
                alert('✓ OpenRouter API is accessible');
            } else {
                alert('✗ OpenRouter API error: ' + (data.error || 'unknown'));
            }
        }
    } catch (err) {
        alert('Error: ' + err.message);
    }
}

async function proxySaveConfig() {
    const ollamaEndpoint = document.getElementById('proxy-ollama-endpoint').value;
    const openrouterKey = document.getElementById('proxy-openrouter-key').value;

    try {
        const res = await fetch('/api/proxy/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                ollama_endpoint: ollamaEndpoint,
                openrouter_key: openrouterKey
            })
        });
        if (res.ok) {
            logTerminal('✓ Proxy configuration saved', 'info');
            alert('Configuration saved');
        } else {
            alert('Failed to save configuration');
        }
    } catch (err) {
        alert('Error: ' + err.message);
    }
}

// ── OTA Firmware Flash ────────────────────────────────────────────────────────

function otaUpdateDeviceList(peers) {
    const el = document.getElementById('ota-device-list');
    if (!el) return;
    if (!peers || peers.length === 0) { el.innerHTML = '<span class="text-dim">No devices online</span>'; return; }
    el.innerHTML = peers.map(p => {
        const ip = p.ip || p.ipAddr || '';
        const ver = p.ver || p.version || '?';
        const name = p.name || p.node_id || p.nodeId || ip;
        if (!ip) return '';
        return `<label class="flex gap-2 items-center mb-1 cursor-pointer"><input type="checkbox" class="ota-device-cb" data-ip="${ip}" checked><span>${name}</span><span class="text-dim ml-auto">${ver}&nbsp;·&nbsp;${ip}</span></label>`;
    }).join('');
}

async function otaFlashSelected() {
    const env = document.getElementById('ota-env').value;
    const ips = Array.from(document.querySelectorAll('.ota-device-cb:checked')).map(cb => cb.dataset.ip).filter(Boolean);
    if (ips.length === 0) { otaLog('No devices selected', 'warn'); return; }
    otaLog(`Flashing ${ips.length} device(s) with ${env}...`);
    for (const ip of ips) {
        try {
            const res = await fetch('/api/ota/flash', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({env, ip}) });
            const data = await res.json();
            if (data.ok) { otaLog(`→ ${ip} started (job ${data.job_id})`); otaPollJob(data.job_id, ip); }
            else otaLog(`✗ ${ip}: ${data.error || 'failed'}`, 'err');
        } catch (e) { otaLog(`✗ ${ip}: ${e.message}`, 'err'); }
    }
}

async function otaPollJob(jobId, ip) {
    for (let i = 0; i < 60; i++) {
        await new Promise(r => setTimeout(r, 3000));
        try {
            const d = await (await fetch(`/api/ota/status/${jobId}`)).json();
            if (d.status === 'done') { otaLog(`✓ ${ip} done — ${d.version || ''}`, 'ok'); return; }
            if (d.status === 'error') { otaLog(`✗ ${ip}: ${d.error}`, 'err'); return; }
        } catch (_) {}
    }
    otaLog(`⚠ ${ip} timed out`, 'warn');
}

function otaLog(msg, level='info') {
    const el = document.getElementById('ota-log');
    if (!el) return;
    const c = {ok:'#4ade80', err:'#f87171', warn:'#fbbf24', info:'#aaa'};
    const d = document.createElement('div');
    d.style.color = c[level]||'#aaa'; d.textContent = msg;
    el.appendChild(d); el.scrollTop = el.scrollHeight;
}

// Auto-populate OTA device list from daemon fleet endpoint
async function otaRefreshFleet() {
    try {
        const d = await (await fetch('/api/ota/fleet')).json();
        if (d.devices && d.devices.length > 0) otaUpdateDeviceList(d.devices);
    } catch (_) {}
}
// Refresh on load and every 30s
document.addEventListener('DOMContentLoaded', () => { otaRefreshFleet(); setInterval(otaRefreshFleet, 30000); });

// ── System Services Panel (octopus) ──────────────────────────────────────────
async function servicesRefresh() {
    const el = document.getElementById('services-list');
    if (!el) return;
    try {
        const d = await (await fetch('http://localhost:8001/api/services')).json();
        el.innerHTML = Object.values(d).map(s => {
            const dot = s.running ? '🟢' : '⚪';
            const uptime = s.uptime_s ? ` ${s.uptime_s}s` : '';
            const btn = s.running
                ? `<button class="btn sm secondary" style="padding:1px 6px;font-size:10px" onclick="svcAction('${s.name}','stop')">stop</button>`
                : `<button class="btn sm" style="padding:1px 6px;font-size:10px" onclick="svcAction('${s.name}','start')">start</button>`;
            return `<div class="flex gap-2 items-center mb-2">${dot} <span class="flex-1">${s.name}</span><span class="text-dim">${uptime}</span>${btn}</div>`;
        }).join('') || '<span class="text-dim">Daemon not reachable</span>';
    } catch (_) {
        if (el) el.innerHTML = '<span class="text-dim">Daemon offline</span>';
    }
}

async function svcAction(name, action) {
    try {
        await fetch(`http://localhost:8001/api/services/${name}/${action}`, {method:'POST'});
        setTimeout(servicesRefresh, 1500);
    } catch (e) { console.error('svcAction failed', e); }
}

document.addEventListener('DOMContentLoaded', () => { servicesRefresh(); setInterval(servicesRefresh, 15000); });
