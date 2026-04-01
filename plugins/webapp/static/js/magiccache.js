/**
 * MagicCache Dashboard Module
 * Real-time cache explorer with table browsing, querying, and alerting
 */

let _magicCacheConfig = null;
let _magicCacheUrl = 'http://localhost:8200';
let _magicCacheTables = [];
let _magicCacheSelectedTable = null;
let _magicCacheAlerts = [];
let _magicCacheWs = null;

/**
 * Initialize MagicCache module - fetch service status and configure
 */
async function initMagicCache() {
    try {
        // Check if MagicCache service is available via daemon
        const servicesResp = await fetch('/api/services/status');
        if (!servicesResp.ok) return false;

        const services = await servicesResp.json();
        if (!services.magiccache || !services.magiccache.enabled) {
            console.log('[MagicCache] Service disabled in configuration');
            return false;
        }

        // Determine MagicCache URL (usually port 8200 on same host)
        _magicCacheUrl = `http://${location.hostname}:${services.magiccache.port || 8200}`;

        // Test connectivity
        const healthResp = await fetch(`${_magicCacheUrl}/health`);
        if (!healthResp.ok) {
            console.warn('[MagicCache] Service health check failed');
            return false;
        }

        console.log('[MagicCache] Service initialized at', _magicCacheUrl);
        loadMagicCacheTables();
        connectMagicCacheWebSocket();
        return true;
    } catch (e) {
        console.warn('[MagicCache] Initialization failed:', e);
        return false;
    }
}

/**
 * Load available tables from MagicCache
 */
async function loadMagicCacheTables() {
    try {
        const resp = await fetch(`${_magicCacheUrl}/tables`);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

        const data = await resp.json();
        _magicCacheTables = data.tables || [];
        renderMagicCacheTableList();
    } catch (e) {
        console.error('[MagicCache] Failed to load tables:', e);
    }
}

/**
 * Load data from selected table
 */
async function loadMagicCacheTable(tableName) {
    try {
        _magicCacheSelectedTable = tableName;
        const resp = await fetch(`${_magicCacheUrl}/tables/${tableName}`);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

        const data = await resp.json();
        renderMagicCacheTableData(data);
    } catch (e) {
        console.error('[MagicCache] Failed to load table:', e);
    }
}

/**
 * Query table with optional filters
 */
async function queryMagicCache(filters = {}) {
    if (!_magicCacheSelectedTable) return;

    try {
        const filterStr = Object.keys(filters).length > 0 ? JSON.stringify(filters) : null;
        const url = new URL(`${_magicCacheUrl}/query`);
        url.searchParams.set('table', _magicCacheSelectedTable);
        if (filterStr) url.searchParams.set('filters', filterStr);

        const resp = await fetch(url.toString());
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

        const data = await resp.json();
        renderMagicCacheQueryResults(data);
    } catch (e) {
        console.error('[MagicCache] Query failed:', e);
    }
}

/**
 * Export table data
 */
async function exportMagicCache(format = 'json') {
    if (!_magicCacheSelectedTable) return;

    try {
        const url = new URL(`${_magicCacheUrl}/export`);
        url.searchParams.set('table', _magicCacheSelectedTable);
        url.searchParams.set('format', format);

        const resp = await fetch(url.toString());
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

        const data = await resp.json();
        const content = format === 'csv' ? data.data : JSON.stringify(data.data, null, 2);
        downloadFile(content, `${_magicCacheSelectedTable}_${data.export_timestamp}.${format}`);
    } catch (e) {
        console.error('[MagicCache] Export failed:', e);
    }
}

/**
 * Load and display alerts
 */
async function loadMagicCacheAlerts() {
    try {
        const resp = await fetch(`${_magicCacheUrl}/alerts`);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);

        const data = await resp.json();
        _magicCacheAlerts = data.alerts || {};
        renderMagicCacheAlerts();
    } catch (e) {
        console.error('[MagicCache] Failed to load alerts:', e);
    }
}

/**
 * Create new alert
 */
async function createMagicCacheAlert(name, condition) {
    try {
        const resp = await fetch(`${_magicCacheUrl}/alerts`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, condition, enabled: true })
        });

        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        loadMagicCacheAlerts();
    } catch (e) {
        console.error('[MagicCache] Failed to create alert:', e);
    }
}

/**
 * Delete alert
 */
async function deleteMagicCacheAlert(alertId) {
    try {
        const resp = await fetch(`${_magicCacheUrl}/alerts/${alertId}`, {
            method: 'DELETE'
        });

        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        loadMagicCacheAlerts();
    } catch (e) {
        console.error('[MagicCache] Failed to delete alert:', e);
    }
}

/**
 * Connect WebSocket for real-time updates
 */
function connectMagicCacheWebSocket() {
    try {
        const proto = location.protocol === 'https:' ? 'wss' : 'ws';
        _magicCacheWs = new WebSocket(`${proto}://${location.hostname}:8200/ws`);

        _magicCacheWs.onopen = () => {
            console.log('[MagicCache] WebSocket connected');
            _magicCacheWs.send('subscribe:all');
        };

        _magicCacheWs.onmessage = (e) => {
            const data = JSON.parse(e.data);
            if (data.type === 'update') {
                onMagicCacheUpdate(data.data);
            }
        };

        _magicCacheWs.onclose = () => {
            console.warn('[MagicCache] WebSocket disconnected');
            // Retry after 5 seconds
            setTimeout(connectMagicCacheWebSocket, 5000);
        };
    } catch (e) {
        console.error('[MagicCache] WebSocket connection failed:', e);
    }
}

/**
 * Handle incoming real-time updates
 */
function onMagicCacheUpdate(data) {
    // Update table data if currently viewing that table
    if (_magicCacheSelectedTable === data.table) {
        loadMagicCacheTable(_magicCacheSelectedTable);
    }
    // Check and trigger alerts
    checkMagicCacheAlerts(data);
}

/**
 * Evaluate alerts against incoming data
 */
function checkMagicCacheAlerts(data) {
    Object.entries(_magicCacheAlerts).forEach(([alertId, alert]) => {
        if (!alert.enabled) return;
        // Simple condition check (can be extended with expression evaluator)
        try {
            const triggered = evalAlertCondition(alert.condition, data);
            if (triggered && !alert.triggered) {
                triggerMagicCacheAlert(alertId, alert, data);
            }
        } catch (e) {
            console.error(`[MagicCache] Alert evaluation failed for ${alertId}:`, e);
        }
    });
}

/**
 * Simple condition evaluator
 */
function evalAlertCondition(condition, data) {
    const match = condition.match(/(\w+)\s*([<>=!]+)\s*(.+)/);
    if (!match) return false;

    const [, field, op, value] = match;
    const dataValue = data[field.trim()];
    const compareValue = parseFloat(value.trim()) || value.trim();

    switch (op) {
        case '<': return dataValue < compareValue;
        case '>': return dataValue > compareValue;
        case '<=': return dataValue <= compareValue;
        case '>=': return dataValue >= compareValue;
        case '==': return dataValue == compareValue;
        case '!=': return dataValue != compareValue;
        default: return false;
    }
}

/**
 * Fire alert notification
 */
function triggerMagicCacheAlert(alertId, alert, data) {
    console.warn(`[MagicCache Alert] ${alert.name}:`, alert.condition, 'matched with', data);

    // Create notification UI element
    const notif = document.createElement('div');
    notif.className = 'magiccache-alert-notif';
    const body = document.createElement('div');
    body.className = 'alert-body';
    const title = document.createElement('div');
    title.className = 'alert-title';
    title.textContent = alert.name;
    const detail = document.createElement('div');
    detail.className = 'alert-detail';
    detail.textContent = JSON.stringify(data);
    body.appendChild(title);
    body.appendChild(detail);
    notif.appendChild(body);

    const container = document.getElementById('magiccache-alerts-container') || document.body;
    container.appendChild(notif);

    setTimeout(() => notif.remove(), 5000);
}

/**
 * Render table list in sidebar
 */
function renderMagicCacheTableList() {
    const container = document.getElementById('magiccache-tables');
    if (!container) return;

    const items = _magicCacheTables.map(name => {
        const div = document.createElement('div');
        div.className = `magiccache-table-item ${_magicCacheSelectedTable === name ? 'active' : ''}`;
        div.textContent = `📊 ${name}`;
        div.onclick = () => loadMagicCacheTable(name);
        return div;
    });

    container.innerHTML = '';
    items.forEach(item => container.appendChild(item));
}

/**
 * Render table data
 */
function renderMagicCacheTableData(tableData) {
    const container = document.getElementById('magiccache-content');
    if (!container) return;

    const records = tableData.records || {};
    container.innerHTML = '';

    const header = document.createElement('div');
    header.className = 'magiccache-header';
    const title = document.createElement('h3');
    title.textContent = tableData.table;
    const stats = document.createElement('div');
    stats.className = 'magiccache-stats';
    stats.textContent = `${tableData.count} records`;
    header.appendChild(title);
    header.appendChild(stats);

    const controls = document.createElement('div');
    controls.className = 'magiccache-controls';

    const jsonBtn = document.createElement('button');
    jsonBtn.className = 'btn sm';
    jsonBtn.textContent = '📥 JSON';
    jsonBtn.onclick = () => exportMagicCache('json');

    const csvBtn = document.createElement('button');
    csvBtn.className = 'btn sm';
    csvBtn.textContent = '📊 CSV';
    csvBtn.onclick = () => exportMagicCache('csv');

    const alertBtn = document.createElement('button');
    alertBtn.className = 'btn sm';
    alertBtn.textContent = '🔔 Alerts';
    alertBtn.onclick = () => loadMagicCacheAlerts();

    controls.appendChild(jsonBtn);
    controls.appendChild(csvBtn);
    controls.appendChild(alertBtn);

    const data = document.createElement('div');
    data.className = 'magiccache-data';
    const pre = document.createElement('pre');
    pre.textContent = JSON.stringify(records, null, 2);
    data.appendChild(pre);

    container.appendChild(header);
    container.appendChild(controls);
    container.appendChild(data);
}

/**
 * Utility: Download file
 */
function downloadFile(content, filename) {
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
}

// Auto-initialize on module load
document.addEventListener('DOMContentLoaded', () => {
    if (document.readyState === 'loading') return;
    initMagicCache();
});
