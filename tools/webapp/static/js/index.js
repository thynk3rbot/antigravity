/**
 * LoRaLink Workspace Engine v2.0
 * Unified logic for Cockpit, Mesh, Scheduler, and Files.
 */

let _socket = null;
let _nodes = new Map();
let _activeNodeId = null;
let _currentModule = 'cockpit';
let _lastStatus = null;
let _sequences = [];
let _mqttClient = null;

// ── Initialization ──────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    initNavigation();
    initWebSocket();
    startTimeUpdate();
    loadNodes();
    loadSequences();
    loadFiles();
    loadBoardVisual("heltec_v3");
    loadDiscovery();
    setInterval(loadDiscovery, 10000); 
});

function initNavigation() {
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', () => {
            const module = item.getAttribute('data-module');
            switchModule(module);
        });
    });
}

function switchModule(module) {
    _currentModule = module;
    document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
    document.querySelector(`[data-module="${module}"]`).classList.add('active');

    document.querySelectorAll('.module-container').forEach(c => c.classList.remove('active'));
    document.getElementById(`mod-${module}`).classList.add('active');

    const titles = {
        cockpit: "Primary Cockpit",
        mesh: "Mesh Swarm (MQTT)",
        scheduler: "IO Task Scheduler",
        files: "Filesystem Browser",
        admin: "System Admin",
        spectrum: "RF Spectrum Analysis",
        prototype: "Prototype Hub & Simulation"
    };
    document.getElementById('view-title').textContent = titles[module] || "LoRaLink";

    if (module === 'mesh') loadDiscovery();
    if (module === 'mesh' && !_mqttClient) initMqtt();
    if (module === 'spectrum') initSpectrum();
    if (module === 'prototype') startPrototypeUpdates(); else stopPrototypeUpdates();
}

function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    _socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
    
    _socket.onopen = () => {
        document.getElementById('connection-dot').style.background = 'var(--green-ok)';
        document.getElementById('connection-dot').style.boxShadow = '0 0 10px var(--green-ok)';
        logTrace("WS connected.");
    };

    _socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        handleMessage(data);
    };

    _socket.onclose = () => {
        document.getElementById('connection-dot').style.background = 'var(--red-err)';
        logTrace("WS disconnected. Retrying...");
        setTimeout(initWebSocket, 2000);
    };
}

function handleMessage(data) {
    if (data.type === 'status') {
        _lastStatus = data;
        updateUI(data);
    } else if (data.type === 'log' || data.type === 'serial_log') {
        logTrace(data.msg || data.text);
    } else if (data.type === 'nodes') {
        updateNodeSelector(data.nodes);
    } else if (data.type === 'mqtt_raw') {
        processMqttMessage(data.topic, data.payload);
    } else if (data.type === 'ai_status') {
        updateAiStatus(data);
    }
}

// ── UI Updates ──────────────────────────────────────────────────

function updateUI(status) {
    if (_currentModule === 'cockpit') {
        const setVal = (id, val) => {
            const el = document.getElementById(id);
            if (el) el.textContent = val;
        };
        setVal('stat-bat', status.bat ? `${status.bat.toFixed(2)}V` : '--');
        setVal('stat-rssi', status.rssi ? `${status.rssi} dBm` : '--');
        setVal('stat-uptime', status.uptime || '--');
        setVal('stat-heap', status.heap ? Math.round(status.heap/1024) + ' KB' : '--');

        const badges = document.getElementById("industrial-badges");
        if (badges) {
            badges.innerHTML = `
                <span class="badge ${status.lora ? 'on' : 'off'}">LoRa</span>
                <span class="badge ${status.espnow ? 'on' : 'off'}">ESP-NOW</span>
                <span class="badge ${status.power_mode === 'SOLAR' ? 'warn' : 'on'}">${status.power_mode || 'LINE'}</span>
            `;
        }
        renderGpioGrid(status.pins || []);
    }

    const updateLink = (id, ok) => {
        const el = document.getElementById(id);
        if (el) el.classList.toggle('active', ok);
    };
    updateLink('link-http', status.http_ok);
    updateLink('link-ble', status.ble_ok);
    updateLink('link-serial', status.serial_ok);
    updateLink('link-mqtt', status.mqtt_ok);

    const fwVerEl = document.getElementById("fw-ver");
    if (fwVerEl && status.version) {
        let ver = String(status.version);
        if (!ver.startsWith('v')) ver = 'v' + ver;
        fwVerEl.textContent = ver;
    }
}

function logTrace(msg) {
    const el = document.getElementById('log-output');
    if (!el) return;
    const line = document.createElement('div');
    line.style.padding = '2px 0';
    line.style.borderBottom = '1px solid rgba(255,255,255,0.05)';
    line.innerHTML = `<span style="color:var(--text-dim)">[${new Date().toLocaleTimeString()}]</span> ${msg}`;
    el.prepend(line);
    if (el.childNodes.length > 50) el.removeChild(el.lastChild);
}

function updateAiStatus(data) {
    const el = document.getElementById('ai-pulse');
    if (el) {
        el.className = `ai-pulse ${data.status}`;
        el.title = `AI Status: ${data.status.toUpperCase()}`;
    }
    if (data.status === 'querying') {
        logTrace(`<span style="color:var(--accent-purple)">[AI] Processing prompt: "${data.prompt}"...</span>`);
    } else if (data.status === 'idle' && data.response) {
        logTrace(`<span style="color:var(--accent-purple)">[AI] Response: ${data.response}</span>`);
    }
}

// ── Hardware Metadata (Legacy Port) ──────────────────────────────────────────
const PIN_METADATA = {
    J3: [
        { pos: 1, label: "GND", type: "gnd" },
        { pos: 2, label: "3V3", type: "pwr" },
        { pos: 3, label: "3V3", type: "pwr" },
        { pos: 4, label: "ADC1_CH6", gpio: 37, type: "adc" },
        { pos: 5, label: "GPIO46", gpio: 46, type: "gpio" },
        { pos: 6, label: "GPIO45", gpio: 45, type: "gpio" },
        { pos: 7, label: "GPIO42", gpio: 42, type: "gpio" },
        { pos: 8, label: "GPIO41", gpio: 41, type: "gpio" },
        { pos: 9, label: "GPIO40", gpio: 40, type: "gpio" },
        { pos: 10, label: "GPIO39", gpio: 39, type: "gpio" },
        { pos: 11, label: "GPIO38", gpio: 38, type: "gpio" },
        { pos: 12, label: "ADC1_CH0", gpio: 1, type: "adc" },
        { pos: 13, label: "ADC1_CH1", gpio: 2, type: "adc" },
        { pos: 14, label: "ADC1_CH2", gpio: 3, type: "adc" },
        { pos: 15, label: "ADC1_CH3", gpio: 4, type: "adc" },
        { pos: 16, label: "ADC1_CH4", gpio: 5, type: "adc" },
        { pos: 17, label: "ADC1_CH5", gpio: 6, type: "adc" },
        { pos: 18, label: "GPIO7", gpio: 7, type: "gpio" },
    ],
    J2: [
        { pos: 1, label: "GND", type: "gnd" },
        { pos: 2, label: "5V", type: "pwr" },
        { pos: 3, label: "Ve", type: "pwr" },
        { pos: 4, label: "Ve", type: "pwr" },
        { pos: 5, label: "U0RXD", gpio: 44, type: "fix" },
        { pos: 6, label: "U0TXD", gpio: 43, type: "fix" },
        { pos: 7, label: "RST", type: "fix" },
        { pos: 8, label: "PRG", gpio: 0, type: "fix" },
        { pos: 9, label: "Vext_Ctrl", gpio: 36, type: "fix" },
        { pos: 10, label: "LED_Write", gpio: 35, type: "fix" },
        { pos: 11, label: "GPIO34", gpio: 34, type: "gpio" },
        { pos: 12, label: "GPIO33", gpio: 33, type: "gpio" },
        { pos: 13, label: "OLED_RST", gpio: 21, type: "fix" },
        { pos: 14, label: "GPIO47", gpio: 47, type: "gpio" },
        { pos: 15, label: "GPIO48", gpio: 48, type: "gpio" },
        { pos: 16, label: "SPICS1", gpio: 26, type: "fix" },
        { pos: 17, label: "GPIO20", gpio: 20, type: "gpio" },
        { pos: 18, label: "GPIO19", gpio: 19, type: "gpio" },
    ],
};

const GPIO_CONTROLS = [
    { label: "RELAY1", pin: "5", type: "relay", desc: "110V Relay" },
    { label: "RELAY2", pin: "6", type: "relay", desc: "12V Relay 2" },
    { label: "LED", pin: "LED", type: "led", desc: "Onboard LED" },
    { label: "VEXT", pin: "36", type: "power", desc: "3.3V Ext Rail", activeLow: true },
    { label: "PWM33", pin: "33", type: "pwm", desc: "Dimmer/Fan" },
    { label: "SRV19", pin: "19", type: "servo", desc: "Servo (0-180°)" },
];

let _pinStatus = {};
let _boardData = null;
let _editingTaskIdx = -1;

// ── Discovery & Fleet ──────────────────────────────────────────────────

async function loadDiscovery() {
    try {
        const res = await fetch('/api/discovery');
        const data = await res.json();
        updateDiscoveryUI(data);
    } catch (e) {
        console.warn("Discovery failed", e);
    }
}

function updateDiscoveryUI(data) {
    if (_currentModule !== 'mesh') return;
    const grid = document.getElementById('mesh-grid');
    if (!grid) return;

    let html = '';
    
    // Discovered WiFi Nodes (mDNS)
    data.mdns.forEach(node => {
        const isRegistered = Array.from(_nodes.values()).some(n => n.type === 'wifi' && (n.address === node.ip || n.name === node.name));
        if (isRegistered) return; // Skip already registered in discovery view

        html += `
            <div class="suit-card-mini active">
                <div class="flex-row-sb">
                    <span class="mg-label">NODE: ${node.name}</span>
                    <span class="ind-badge active">WIFI</span>
                </div>
                <div class="mg-value" style="font-size: 11px; margin-top: 4px;">${node.ip}</div>
                <div class="mt-8">
                    <button class="btn btn-xs" style="width:100%" onclick="registerNode('${node.name}', 'wifi', '${node.ip}')">REGISTER</button>
                </div>
            </div>
        `;
    });

    // Serial Ports
    data.serial.forEach(port => {
        const isRegistered = Array.from(_nodes.values()).some(n => n.type === 'serial' && n.address === port);
        const cardClass = isRegistered ? 'suit-card-mini' : 'suit-card-mini active';
        html += `
            <div class="${cardClass}">
                <div class="flex-row-sb">
                    <span class="mg-label">SERIAL PORT</span>
                    <span class="ind-badge active">USB</span>
                </div>
                <div class="mg-value" style="font-size: 11px; margin-top: 4px;">${port}</div>
                <div class="mt-8">
                    ${isRegistered ? 
                        '<button class="btn btn-xs btn-dim" style="width:100%" disabled>REGISTERED</button>' : 
                        `<button class="btn btn-xs" style="width:100%" onclick="registerNode('USB-${port.slice(-5)}', 'serial', '${port}')">USE</button>`}
                </div>
            </div>
        `;
    });

    if (data.mdns.length === 0 && data.serial.length === 0) {
        html += '<div class="list-empty">No new assets discovered. Scanning...</div>';
    }

    grid.innerHTML = html;
}

async function rebootSwarm() {
    if (!confirm("Broadcast REBOOT to all fleet devices?")) return;
    try {
        logTrace("Sending reboot-swarm command...");
        const res = await fetch('/api/reboot-swarm', { method: 'POST' });
        const data = await res.json();
        logTrace(`Swarm results: ${data.results.length} nodes signaled.`);
        data.results.forEach(r => {
            logTrace(` - ${r.name} (${r.type}): ${r.ok ? 'OK' : 'FAIL'}`);
        });
    } catch (e) {
        logTrace("Swarm reboot failed.");
    }
}

async function registerNode(name, type, address) {
    try {
        await fetch('/api/discovery/register', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({name, type, address})
        });
        loadNodes();
    } catch (e) {
        alert("Registration failed");
    }
}

// ── HUD & Cockpit ────────────────────────────────────────────────────────────
// The following functions updateHUD, updateTransportBadge, updateCockpit, renderGpioGrid, togglePin, setText
// are largely redundant with the existing updateUI function and its sub-components.
// I will integrate the new parts (GPIO grid, industrial badges) into the existing updateUI.

function updateUI(status) {
    if (_currentModule === 'cockpit') {
        const setVal = (id, val) => {
            const el = document.getElementById(id);
            if (el) el.textContent = val;
        };
        setVal('stat-bat', status.bat ? `${status.bat.toFixed(2)}V` : '--');
        setVal('stat-rssi', status.rssi ? `${status.rssi} dBm` : '--');
        setVal('stat-uptime', status.uptime || '--');
        setVal('stat-heap', status.heap ? Math.round(status.heap/1024) + ' KB' : '--');

        // Industrial badges
        const badges = document.getElementById("industrial-badges");
        if (badges) {
            badges.innerHTML = `
                <span class="badge ${status.lora ? 'on' : 'off'}">LoRa</span>
                <span class="badge ${status.espnow ? 'on' : 'off'}">ESP-NOW</span>
                <span class="badge ${status.power_mode === 'SOLAR' ? 'warn' : 'on'}">${status.power_mode || 'LINE'}</span>
            `;
        }
        renderGpioGrid(status.pins || []);
    }

    // Transport HUD
    const updateLink = (id, ok) => {
        const el = document.getElementById(id);
        if (el) el.classList.toggle('active', ok);
    };
    updateLink('link-http', status.http_ok);
    updateLink('link-ble', status.ble_ok);
    updateLink('link-serial', status.serial_ok);
    updateLink('link-mqtt', status.mqtt_ok);

    const fwVerEl = document.getElementById("fw-ver");
    if (fwVerEl && status.version) {
        let ver = String(status.version);
        if (!ver.startsWith('v')) ver = 'v' + ver;
        fwVerEl.textContent = ver;
    }
}

function onEditorTypeChange() {
    const type = document.getElementById("edit-task-type").value;
    const durField = document.getElementById("duration-field-group");
    if (durField) {
        durField.style.display = (type === "PULSE" || type === "TOGGLE") ? "block" : "none";
    }
}

function renderGpioGrid(pins) {
    const grid = document.getElementById("gpio-grid");
    if (!grid) return;
    
    // Create grid if empty
    if (grid.children.length === 0) {
        GPIO_CONTROLS.forEach(ctrl => {
            const card = document.createElement("div");
            card.className = "gpio-card";
            card.innerHTML = `
                <div class="gpio-label">${ctrl.label}</div>
                <div class="gpio-desc">${ctrl.desc}</div>
                <div class="gpio-action">
                    <button class="btn btn-sm" onclick="togglePin('${ctrl.pin}')">TOGGLE</button>
                </div>
            `;
            card.id = `gpio-ctrl-${ctrl.pin}`;
            grid.appendChild(card);
        });
    }

    // Update states
    pins.forEach(p => {
        const card = document.getElementById(`gpio-ctrl-${p.id}`);
        if (card) {
            const isHigh = p.val === 1;
            card.classList.toggle("active", isHigh);
        }
    });
}

function togglePin(pin) {
    sendCommand(`TOGGLE ${pin}`);
}

// ── Board Visualizer ─────────────────────────────────────────────────────────
async function loadBoardVisual(hwId) {
    const container = document.getElementById("board-visual-container");
    if (!container) return;
    try {
        const resp = await fetch(`/api/boards/${hwId}`);
        _boardData = await resp.json();
        renderBoardSvg();
    } catch (e) {
        container.innerHTML = `<div class="list-empty">Board ${hwId} not found.</div>`;
    }
}

function renderBoardSvg() {
    if (!_boardData) return;
    const container = document.getElementById("board-visual-container");
    const { width, height, headers } = _boardData;

    let content = "";
    headers.forEach(h => {
        h.pins.forEach((p, i) => {
            const x = h.start_x;
            const y = h.start_y + (i * h.pitch);
            content += `<circle cx="${x}" cy="${y}" r="2.5" class="board-pin" data-gpio="${p.gpio || ''}" />`;
            const labelX = h.side === 'left' ? x + 5 : x - 5;
            content += `<text x="${labelX}" y="${y+1.5}" class="board-pin-label ${h.side === 'right' ? 'text-right' : ''}">${p.label}</text>`;
        });
    });

    container.innerHTML = `<svg viewBox="0 0 ${width} ${height}" class="board-svg">
        <rect x="0" y="0" width="${width}" height="${height}" rx="4" fill="#0c0d10" stroke="#2d333b" />
        ${content}
    </svg>`;
}

function updateBoardHighlight() {
    document.querySelectorAll(".board-pin").forEach(el => {
        const gpio = el.getAttribute("data-gpio");
        if (gpio && _pinStatus[gpio] !== undefined) {
            el.style.fill = _pinStatus[gpio] === 1 ? "var(--green-ok)" : "#222"; // Changed var(--ok) to var(--green-ok)
            el.style.filter = _pinStatus[gpio] === 1 ? "drop-shadow(0 0 3px var(--green-ok))" : "none"; // Changed var(--ok) to var(--green-ok)
        }
    });
}

// ── Actions ───────────────────────────────────────────────────

async function runCommand() {
    const inp = document.getElementById('cmd-input');
    const cmd = inp.value.trim();
    if (!cmd) return;
    inp.value = '';
    sendCommand(cmd);
}

async function sendCommand(cmd, nodeId = null) {
    logTrace(`<span style="color:var(--accent-blue)">> ${cmd}</span>`);
    try {
        const resp = await fetch('/api/cmd', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ cmd, node_id: nodeId || _activeNodeId })
        });
        const res = await resp.text();
        if (res === 'ERR') logTrace(`<span style="color:var(--red-err)">! Command failed</span>`);
    } catch (e) {
        logTrace(`! Network error: ${e.message}`);
    }
}

// ── Shared Logic ────────────────────────────────────────────────

function startTimeUpdate() {
    setInterval(() => {
        document.getElementById('system-time').textContent = new Date().toLocaleTimeString();
    }, 1000);
}

async function loadNodes() {
    const r = await fetch('/api/nodes');
    const data = await r.json();
    _nodes.clear();
    const nodeList = data.nodes || [];
    nodeList.forEach(n => _nodes.set(n.id, n));
    updateNodeSelector(nodeList);
}

function updateNodeSelector(nodes) {
    const sel = document.getElementById('active-node-select');
    if (!sel) return;
    const current = sel.value;
    
    sel.innerHTML = nodes.map(n => {
        const statusText = n.online ? "" : " [OFFLINE]";
        const style = n.online ? "" : "color:var(--text-dim)";
        return `<option value="${n.id}" ${n.id === current ? 'selected' : ''} style="${style}">${n.name}${statusText} (${n.type})</option>`;
    }).join('');
    
    // Auto-select first if none active
    if (!_activeNodeId && nodes.length) {
        const firstOnline = nodes.find(n => n.online) || nodes[0];
        _activeNodeId = firstOnline.id;
    }

    // Refresh cockpit if active node changed status
    const active = nodes.find(n => n.id === _activeNodeId);
    if (active && !active.online) {
        document.body.classList.add('node-offline');
    } else {
        document.body.classList.remove('node-offline');
    }
}

function switchTargetNode(id) {
    _activeNodeId = id;
    logTrace(`Focus shifted to node: ${id}`);
    // Optional: push to server to update transport target
}

// ── Module: Mesh (MQTT) ────────────────────────────────────────

function initMqtt() {
    console.log("Initializing local MQTT bridge...");
    // We already have MQTT coming in via WebSockets from server.py's MqttManager
    // So we just need to render the data.
}

function processMqttMessage(topic, payload) {
    const parts = topic.split('/');
    const id = parts[parts.length - 1];
    
    let card = document.getElementById(`mesh-card-${id}`);
    if (!card) {
        const grid = document.getElementById('mesh-grid');
        const empty = grid.querySelector('.list-empty');
        if (empty) grid.innerHTML = ''; 
        
        card = document.createElement('div');
        card.id = `mesh-card-${id}`;
        card.className = 'suit-card-mini active';
        grid.appendChild(card);
    }

    try {
        const data = JSON.parse(payload);
        card.innerHTML = `
            <div class="flex-row-sb mb-8">
                <span class="mg-label">ID: ${id}</span>
                <span class="ind-badge active">MQTT</span>
            </div>
            <div class="gauge-cluster" style="padding:0; gap:4px;">
                <div class="mini-gauge" style="min-width:0; padding:4px 8px; flex:1;">
                    <div class="mg-data">
                        <span class="mg-label">RSSI</span>
                        <span class="mg-value" style="font-size:11px">${data.rssi || '--'}</span>
                    </div>
                </div>
                <div class="mini-gauge" style="min-width:0; padding:4px 8px; flex:1;">
                    <div class="mg-data">
                        <span class="mg-label">BAT</span>
                        <span class="mg-value" style="font-size:11px">${data.battery || '--'}V</span>
                    </div>
                </div>
            </div>
            <div class="mt-8">
               <button class="btn btn-xs" style="width:100%" onclick="sendCommand('PING', '${id}')">PING NODE</button>
            </div>
        `;
    } catch(e) {
        card.innerHTML = `<div class="mg-label">ID: ${id}</div><div class="mg-value" style="font-size: 10px; overflow: hidden; text-overflow: ellipsis;">${payload}</div>`;
    }
}

// ── Module: Scheduler ──────────────────────────────────────────

async function loadSequences() {
    try {
        const r = await fetch('/api/sequences');
        _sequences = await r.json();
        renderScheduler();
    } catch(e) { console.error("Sequence load failed", e); }
}

function renderScheduler() {
    const container = document.getElementById('scheduler-content');
    if (!container) return;
    
    // Check if we already have the structural layout
    if (!container.querySelector('.schedule-grid')) {
        container.innerHTML = `
            <div class="flex-row-sb mb-16">
                <h3 id="current-seq-name">Active Sequence: Main</h3>
                <button class="btn btn-sm" onclick="openTaskEditor(-1)">+ NEW TASK</button>
            </div>
            <div class="grid" id="schedule-grid"></div>
            <div class="mt-24 border-top pt-16 flex-row-sb">
                <button class="btn btn-secondary" onclick="loadSequences()">RELOAD</button>
                <button class="btn" onclick="saveActiveSequence()">SAVE TO FLASH</button>
            </div>
        `;
    }
    updateScheduleGrid();
}

function updateScheduleGrid() {
    const grid = document.getElementById('schedule-grid');
    if (!grid) return;
    
    // Flatten sequences for the grid
    const tasks = _sequences.flatMap(s => s.tasks.map((t, idx) => ({...t, seqName: s.name, originalIdx: idx})));
    
    if (tasks.length === 0) {
        grid.innerHTML = '<div class="list-empty">No tasks defined in this sequence.</div>';
        return;
    }

    grid.innerHTML = tasks.map((t, i) => `
        <div class="card" onclick="openTaskEditor(${i})">
            <div class="card-header">${t.name} <span class="badge on">${t.type}</span></div>
            <div style="font-size: 0.8rem; color: var(--text-dim);">
                <div>Pin: <b style="color:var(--text-main)">${t.pin}</b></div>
                <div>Interval: <b style="color:var(--text-main)">${t.interval}s</b></div>
                ${t.duration ? `<div>Duration: <b style="color:var(--text-main)">${t.duration}s</b></div>` : ''}
            </div>
            <div class="mt-12 flex-row-sb" style="font-size: 10px; color: var(--accent-blue);">
                <span>Click to Edit</span>
                <i class="fas fa-edit"></i>
            </div>
        </div>
    `).join('');
}

function openTaskEditor(idx) {
    _editingTaskIdx = idx;
    const tasks = _sequences.flatMap(s => s.tasks);
    const t = idx >= 0 ? tasks[idx] : { name: "New Task", type: "TOGGLE", pin: "5", interval: 60, duration: 0 };
    
    document.getElementById("edit-task-name").value = t.name;
    document.getElementById("edit-task-type").value = t.type;
    document.getElementById("edit-task-pin").value = t.pin;
    document.getElementById("edit-task-interval").value = t.interval;
    document.getElementById("edit-task-duration").value = t.duration || 0;
    
    document.getElementById("task-editor-modal").style.display = "flex";
    onEditorTypeChange();
}

function closeTaskEditor() {
    document.getElementById("task-editor-modal").style.display = "none";
}

function saveTaskFromEditor() {
    const t = {
        name: document.getElementById("edit-task-name").value,
        type: document.getElementById("edit-task-type").value,
        pin: document.getElementById("edit-task-pin").value,
        interval: parseInt(document.getElementById("edit-task-interval").value),
        duration: parseInt(document.getElementById("edit-task-duration").value)
    };
    
    // For now, we assume we're editing the first sequence (Main)
    if (!_sequences[0]) _sequences[0] = { name: "Main", tasks: [] };
    
    if (_editingTaskIdx === -1) {
        _sequences[0].tasks.push(t);
    } else {
        _sequences[0].tasks[_editingTaskIdx] = t;
    }
    
    closeTaskEditor();
    updateScheduleGrid();
}

function deleteCurrentTask() {
    if (_editingTaskIdx >= 0 && _sequences[0]) {
        _sequences[0].tasks.splice(_editingTaskIdx, 1);
    }
    closeTaskEditor();
    updateScheduleGrid();
}

async function saveActiveSequence() {
    if (!_sequences[0]) return;
    try {
        await fetch("/api/sequences", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(_sequences[0])
        });
        logTrace("Sequence saved to device.");
    } catch (e) {
        logTrace("!! Save failed: " + e.message);
    }
}

// ── Module: Spectrum ──────────────────────────────────────

let _spectrumInterval = null;

function initSpectrum() {
    const canvas = document.getElementById('spectrum-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    
    if (_spectrumInterval) clearInterval(_spectrumInterval);
    _spectrumInterval = setInterval(() => {
        if (_currentModule !== 'spectrum') return;
        drawSpectrum(ctx, canvas.width, canvas.height);
    }, 100);
}

function drawSpectrum(ctx, w, h) {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, w, h);
    
    // Grid
    ctx.strokeStyle = "#1a2a3a";
    ctx.lineWidth = 1;
    for(let i=0; i<10; i++) {
        const y = (h/10) * i;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }

    // Noise Floor
    ctx.strokeStyle = "rgba(0, 242, 255, 0.3)";
    ctx.beginPath();
    ctx.moveTo(0, h * 0.8);
    for(let x=0; x<w; x+=2) {
        const noise = Math.random() * 20;
        ctx.lineTo(x, h * 0.8 - noise);
    }
    ctx.stroke();

    // Signal Peaks (Simulated LoRa bursts)
    if (Math.random() > 0.95) {
        ctx.strokeStyle = "#00f2ff";
        ctx.lineWidth = 2;
        const peakX = Math.random() * w;
        ctx.beginPath();
        ctx.moveTo(peakX - 10, h * 0.8);
        ctx.lineTo(peakX, h * 0.2 + Math.random()*50);
        ctx.lineTo(peakX + 10, h * 0.8);
        ctx.stroke();
    }
}

// ── Shared Logic (Cont.) ────────────────────────────────────────

async function loadFiles() {
    try {
        const r = await fetch('/api/files/list');
        const data = await r.json();
        const body = document.getElementById('file-body');
        if (!body) return;
        body.innerHTML = data.files.map(f => `
            <tr>
               <td style="padding: 10px; color: var(--accent-blue); cursor:pointer" onclick="viewFile('${f.name}')">${f.name}</td>
               <td style="color: var(--text-dim);">${f.size ? Math.round(f.size/1024) + ' KB' : '--'}</td>
               <td style="text-align: right;">
                  <button class="btn" style="background:rgba(255,0,0,0.1); color:var(--red-err); font-size: 10px;" onclick="deleteFile('${f.name}')">DEL</button>
               </td>
            </tr>
        `).join('');
    } catch(e) { console.error("File load failed", e); }
}

async function viewFile(name) {
    window.open(`/api/files/read?path=/${name}`, '_blank');
}

async function deleteFile(name) {
    if (!confirm(`Permanently delete ${name}?`)) return;
    try {
        await fetch(`/api/files/delete?path=/${name}`, { method: "DELETE" });
        loadFiles();
    } catch(e) { logTrace("!! Delete failed: " + e.message); }
}
// ── Module: Prototype Hub ───────────────────────────────────

let _protoUpdateInterval = null;

function startPrototypeUpdates() {
    refreshPlugins();
    loadIoMatrix();
    if (_protoUpdateInterval) clearInterval(_protoUpdateInterval);
    _protoUpdateInterval = setInterval(() => {
        if (_currentModule === 'prototype') loadIoMatrix();
    }, 2000);
}

function stopPrototypeUpdates() {
    if (_protoUpdateInterval) clearInterval(_protoUpdateInterval);
    _protoUpdateInterval = null;
}

async function refreshPlugins() {
    try {
        const res = await fetch('/api/protodev/plugins');
        const data = await res.json();
        renderPluginList(data.plugins || []);
    } catch (e) {
        console.warn("Plugin refresh failed", e);
    }
}

function renderPluginList(plugins) {
    const el = document.getElementById('plugin-list');
    if (!el) return;
    if (plugins.length === 0) {
        el.innerHTML = '<div class="list-empty">No plugins detected.</div>';
        return;
    }
    el.innerHTML = plugins.map(p => `
        <div class="suit-card-mini active mb-8">
            <div class="flex-row-sb">
                <span class="mg-label"><i class="fas ${p.icon || 'fa-plug'}"></i> ${p.name}</span>
                <span class="ind-badge ${p.status === 'RUNNING' ? 'active' : ''}">${p.status}</span>
            </div>
            <div class="mg-value" style="font-size: 10px; margin-top: 4px;">PINS: ${p.pins ? p.pins.join(', ') : 'NONE'}</div>
        </div>
    `).join('');
}

async function loadIoMatrix() {
    try {
        const res = await fetch('/api/protodev/hal');
        const data = await res.json();
        renderIoMatrix(data.pins || []);
    } catch (e) {
        console.warn("HAL map failed", e);
    }
}

function renderIoMatrix(pins) {
    const el = document.getElementById('io-matrix');
    if (!el) return;
    if (pins.length === 0) {
        // Fallback to generating some default pins for visualization if empty
        const mockPins = [];
        for(let i=0; i<32; i++) mockPins.push({id: i, mode: 'IN', val: 0});
        pins = mockPins;
    }
    el.innerHTML = pins.map(p => `
        <div class="io-pin-box ${p.val ? 'high' : 'low'}" title="GPIO ${p.id} (${p.mode})">
            <div class="pin-id">${p.id}</div>
            <div class="pin-mode">${p.mode}</div>
        </div>
    `).join('');
}

async function toggleSimMode() {
    try {
        const res = await fetch('/api/simulator/toggle', { method: 'POST' });
        const data = await res.json();
        logTrace(`Simulator: ${data.active ? 'STARTED' : 'STOPPED'}`);
        // Visual indicator in Prototype Hub
        document.querySelector('[data-module="prototype"]').classList.toggle('sim-active', data.active);
    } catch (e) {
        logTrace("! Simulator toggle failed");
    }
}

async function sendHalCommand() {
    const inp = document.getElementById('proto-cmd-input');
    const cmd = inp.value.trim();
    if (!cmd) return;
    inp.value = '';
    
    // Log in HAL console
    const consoleEl = document.getElementById('hal-console');
    const line = document.createElement('div');
    line.className = 'hal-log-line';
    line.innerHTML = `<span class="text-accent">></span> ${cmd}`;
    consoleEl.prepend(line);

    try {
        const res = await fetch('/api/protodev/cmd', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ cmd })
        });
        const data = await res.json();
        if (data.resp) {
            const respLine = document.createElement('div');
            respLine.className = 'hal-log-line';
            respLine.innerHTML = `<span class="text-dim"><</span> ${data.resp}`;
            consoleEl.prepend(respLine);
        }
    } catch (e) {
        console.warn("HAL command failed", e);
    }
}
