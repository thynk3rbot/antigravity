/**
 * LMX Messenger PWA — WebSocket chat client
 */

const messagesEl = document.getElementById('messages');
const wsDot      = document.getElementById('ws-dot');
const nodeLabel  = document.getElementById('node-label');
const destInput  = document.getElementById('dest-input');
const msgInput   = document.getElementById('msg-input');
const sendBtn    = document.getElementById('send-btn');

let ws = null;
const msgMap = {};  // packet_id -> DOM element for ACK updates

function formatTime(ts) {
    if (!ts) return '';
    const d = new Date(ts.endsWith('Z') ? ts : ts + 'Z');
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function hexNode(n) {
    return '0x' + (n || 0).toString(16).padStart(2, '0').toUpperCase();
}

function appendMsg(m) {
    const dir = m.direction || m.type;
    const isRx = dir === 'rx';

    const div = document.createElement('div');
    div.className = 'msg ' + (isRx ? 'rx' : 'tx');

    const text = document.createElement('div');
    text.textContent = m.text || m.content || '';
    div.appendChild(text);

    const meta = document.createElement('div');
    meta.className = 'meta';

    const addrPart = isRx
        ? hexNode(m.src) + ' \u2192 me'
        : 'me \u2192 ' + hexNode(m.dest);

    const hopPart = m.hops_used ? ` \u00b7 ${m.hops_used} hop${m.hops_used !== 1 ? 's' : ''}` : '';
    const timePart = m.created_at ? ' \u00b7 ' + formatTime(m.created_at) : '';

    meta.textContent = addrPart + hopPart + timePart;
    div.appendChild(meta);

    messagesEl.appendChild(div);
    messagesEl.scrollTop = messagesEl.scrollHeight;

    const key = m.packet_id || m.id;
    if (key) msgMap[key] = div;
}

function markAcked(packetId) {
    const el = msgMap[packetId];
    if (el) {
        const meta = el.querySelector('.meta');
        if (meta && !meta.textContent.includes('\u2713\u2713')) {
            meta.textContent += ' \u00b7 \u2713\u2713';
        }
    }
}

function connect() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}/ws`);

    ws.onopen = () => {
        wsDot.className = 'status-dot green';
        sendBtn.disabled = false;
    };

    ws.onclose = () => {
        wsDot.className = 'status-dot red';
        sendBtn.disabled = true;
        setTimeout(connect, 3000);
    };

    ws.onmessage = (e) => {
        const data = JSON.parse(e.data);
        if (data.type === 'history') {
            messagesEl.textContent = '';
            (data.messages || []).forEach(appendMsg);
        } else if (data.type === 'rx' || data.type === 'tx') {
            appendMsg(data);
        } else if (data.type === 'ack') {
            markAcked(data.packet_id);
        }
    };
}

async function loadHealth() {
    try {
        const r = await fetch('/health');
        const d = await r.json();
        nodeLabel.textContent = 'node: ' + hexNode(d.node_id);
    } catch (_) {}
}

function sendMessage() {
    const destStr = destInput.value.trim().replace(/^0x/i, '');
    const dest = parseInt(destStr, 16);
    const text = msgInput.value.trim();
    if (!text || isNaN(dest)) return;

    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'send', dest, text }));
        msgInput.value = '';
        msgInput.focus();
    }
}

sendBtn.addEventListener('click', sendMessage);
msgInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') sendMessage();
});

loadHealth();
connect();
