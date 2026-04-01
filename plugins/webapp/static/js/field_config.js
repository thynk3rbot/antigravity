/**
 * field_config.js — Field Configuration Panel
 *
 * Internal admin tool. Edits field_definitions.json via daemon API.
 * Controls which fields appear in STATUS/VSTATUS dashboard views.
 * All DOM manipulation uses safe DOM methods (no innerHTML with user data).
 */

const FC_TYPES = [
    'string','percent','dbm','float','integer','duration',
    'ipv4','millivolts','boolean','bitmask','celsius','bytes','gps'
];

let _fcFields = [];

/**
 * Load field definitions for the selected class/type from the daemon API.
 * Called when the page loads and when class/type selectors change.
 */
async function fcLoad() {
    const cls  = document.getElementById('fc-class').value;
    const type = document.getElementById('fc-type').value;
    const tbody = document.getElementById('fc-tbody');
    _fcClear(tbody, 'Loading\u2026');
    document.getElementById('fc-status').textContent = '';
    try {
        const res = await fetch('/api/config/fields/' + cls + '/all');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        _fcFields = (data[type] || []).map(function(f) { return Object.assign({}, f); });
        fcRender();
    } catch(e) {
        _fcClear(tbody, 'Failed to load: ' + e, '#f55');
    }
}

/** Clear tbody and optionally show a placeholder message. */
function _fcClear(tbody, msg, color) {
    while (tbody.firstChild) tbody.removeChild(tbody.firstChild);
    if (msg) {
        var tr = document.createElement('tr');
        var td = document.createElement('td');
        td.colSpan = 8;
        td.style.cssText = 'text-align:center;padding:20px;color:' + (color || 'var(--text-dim)');
        td.textContent = msg;
        tr.appendChild(td);
        tbody.appendChild(tr);
    }
}

/** Rebuild the table from _fcFields. */
function fcRender() {
    const tbody = document.getElementById('fc-tbody');
    _fcClear(tbody);
    if (!_fcFields.length) {
        _fcClear(tbody, 'No fields defined.');
        return;
    }
    _fcFields.sort(function(a,b){ return (a.order||0) - (b.order||0); });
    _fcFields.forEach(function(f, i) {
        tbody.appendChild(_fcMakeRow(f, i));
    });
}

/** Build a single table row for field[i]. Uses DOM methods only. */
function _fcMakeRow(f, i) {
    var tr = document.createElement('tr');
    tr.style.borderBottom = '1px solid #1a1a1a';

    // Helper: styled input
    function inp(val, style, onChange) {
        var el = document.createElement('input');
        el.value = val;
        el.style.cssText = style;
        el.addEventListener('change', onChange);
        return el;
    }

    // Key
    var tdKey = document.createElement('td');
    tdKey.style.padding = '5px 8px';
    tdKey.appendChild(inp(f.key || '', 'background:#0a0a0a;border:1px solid #333;color:var(--text);padding:3px 6px;width:130px;border-radius:3px;font-family:monospace;font-size:.78rem',
        function(){ _fcSet(i,'key',this.value); }));
    tr.appendChild(tdKey);

    // Label
    var tdLabel = document.createElement('td');
    tdLabel.style.padding = '5px 8px';
    tdLabel.appendChild(inp(f.label || '', 'background:#0a0a0a;border:1px solid #333;color:var(--text);padding:3px 6px;width:150px;border-radius:3px;font-size:.78rem',
        function(){ _fcSet(i,'label',this.value); }));
    tr.appendChild(tdLabel);

    // Type (select)
    var tdType = document.createElement('td');
    tdType.style.padding = '5px 8px';
    var sel = document.createElement('select');
    sel.style.cssText = 'background:#0a0a0a;border:1px solid #333;color:var(--text);padding:3px 4px;border-radius:3px;font-size:.78rem';
    FC_TYPES.forEach(function(t) {
        var opt = document.createElement('option');
        opt.value = t;
        opt.textContent = t;
        if (f.type === t) opt.selected = true;
        sel.appendChild(opt);
    });
    sel.addEventListener('change', function(){ _fcSet(i,'type',this.value); });
    tdType.appendChild(sel);
    tr.appendChild(tdType);

    // Visible checkbox
    tr.appendChild(_fcCheckCell(f.visible, function(){ _fcSet(i,'visible',this.checked); }));

    // Critical checkbox
    tr.appendChild(_fcCheckCell(f.critical, function(){ _fcSet(i,'critical',this.checked); }));

    // Order
    var tdOrd = document.createElement('td');
    tdOrd.style.cssText = 'text-align:center;padding:5px 8px';
    var inpOrd = inp(f.order || 0, 'background:#0a0a0a;border:1px solid #333;color:var(--text);padding:3px 4px;width:50px;text-align:center;border-radius:3px;font-size:.78rem',
        function(){ _fcSet(i,'order',parseInt(this.value)||0); });
    inpOrd.type = 'number';
    tdOrd.appendChild(inpOrd);
    tr.appendChild(tdOrd);

    // Screen page (read-only)
    var tdPage = document.createElement('td');
    tdPage.style.cssText = 'text-align:center;padding:5px 8px';
    var page = (f.page != null) ? f.page : 0;
    tdPage.textContent = page || '\u2014';
    tdPage.style.color = page ? 'var(--accent)' : '#555';
    tr.appendChild(tdPage);

    // Delete button
    var tdDel = document.createElement('td');
    tdDel.style.cssText = 'text-align:center;padding:5px 8px';
    var btn = document.createElement('button');
    btn.textContent = '\u2715';
    btn.title = 'Remove field';
    btn.style.cssText = 'background:none;border:none;color:#f55;cursor:pointer;font-size:.9rem';
    btn.addEventListener('click', (function(idx){ return function(){ _fcDel(idx); }; })(i));
    tdDel.appendChild(btn);
    tr.appendChild(tdDel);

    return tr;
}

function _fcCheckCell(checked, onChange) {
    var td = document.createElement('td');
    td.style.cssText = 'text-align:center;padding:5px 8px';
    var cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.checked = !!checked;
    cb.addEventListener('change', onChange);
    td.appendChild(cb);
    return td;
}

function _fcSet(i, key, val) { _fcFields[i][key] = val; }

function _fcDel(i) {
    _fcFields.splice(i, 1);
    fcRender();
}

/** Append a blank row to the table for a new field. */
function fcAddRow() {
    var maxOrder = _fcFields.reduce(function(m,f){ return Math.max(m, f.order||0); }, 0);
    _fcFields.push({ key:'', label:'', type:'string', visible:true, critical:false, order:maxOrder+1, page:0 });
    fcRender();
    var tbody = document.getElementById('fc-tbody');
    if (tbody.lastChild) tbody.lastChild.scrollIntoView({ behavior:'smooth' });
}

/** PUT current _fcFields to the daemon. */
async function fcSave() {
    const cls  = document.getElementById('fc-class').value;
    const type = document.getElementById('fc-type').value;
    const statusEl = document.getElementById('fc-status');

    var invalid = _fcFields.filter(function(f){ return !f.key || !f.key.trim(); });
    if (invalid.length) {
        statusEl.textContent = '\u2715 All fields must have a key.';
        statusEl.style.color = '#f55';
        return;
    }

    statusEl.textContent = 'Saving\u2026';
    statusEl.style.color = 'var(--text-dim)';
    try {
        const res = await fetch('/api/config/fields/' + cls + '/' + type, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ fields: _fcFields })
        });
        if (!res.ok) {
            const err = await res.json();
            throw new Error(err.detail || res.status);
        }
        statusEl.textContent = '\u2713 Saved \u2014 ' + cls.toUpperCase() + ' ' + type.toUpperCase();
        statusEl.style.color = '#0f0';
    } catch(e) {
        statusEl.textContent = '\u2715 Save failed: ' + e;
        statusEl.style.color = '#f55';
    }
}

/** Reset one class to defaults (reloads from disk). */
async function fcReset() {
    const cls = document.getElementById('fc-class').value;
    if (!confirm('Reset ' + cls.toUpperCase() + ' field definitions to defaults?')) return;
    const statusEl = document.getElementById('fc-status');
    statusEl.textContent = 'Resetting\u2026';
    try {
        const res = await fetch('/api/config/reset/' + cls, { method: 'POST' });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        statusEl.textContent = '\u2713 Reset to defaults';
        statusEl.style.color = '#0f0';
        await fcLoad();
    } catch(e) {
        statusEl.textContent = '\u2715 Reset failed: ' + e;
        statusEl.style.color = '#f55';
    }
}
