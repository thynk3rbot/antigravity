// dashboard.js — LoRaLink MQTT Dashboard
// All untrusted data (client IDs, message payloads) is handled via
// textContent / createElement to prevent XSS from malicious broker clients.

const BROKER_URL = window.MQTT_BROKER_URL || 'ws://localhost:8083/mqtt';
const TOPIC_PREFIX = 'loralink';
const clients = {};

// Returns the text content of a div built with textContent — no HTML injection possible.
function sanitize(str) {
  const d = document.createElement('div');
  d.textContent = String(str);
  return d.textContent;
}

// Builds a client card entirely via DOM API — never uses innerHTML with untrusted data.
function buildClientCard(clientId) {
  const safeId = sanitize(clientId);

  const card = document.createElement('div');
  card.className = 'card client-card';
  card.id = 'card-' + safeId;

  const header = document.createElement('div');
  header.style.cssText = 'display:flex;align-items:center;margin-bottom:.75rem';

  const dot = document.createElement('span');
  dot.className = 'status-dot';
  dot.id = 'dot-' + safeId;

  const name = document.createElement('strong');
  name.style.color = 'var(--text-bright)';
  name.textContent = safeId;

  header.appendChild(dot);
  header.appendChild(name);

  const seenLine = document.createElement('div');
  seenLine.style.cssText = 'color:var(--text-dim);font-size:.8rem;margin-bottom:.5rem';
  seenLine.textContent = 'Last seen: ';
  const seenSpan = document.createElement('span');
  seenSpan.id = 'seen-' + safeId;
  seenSpan.textContent = 'now';
  seenLine.appendChild(seenSpan);

  const msgBox = document.createElement('div');
  msgBox.id = 'msg-' + safeId;
  msgBox.style.cssText = 'background:var(--bg-main);border-radius:6px;padding:.5rem;font-family:monospace;font-size:.8rem;color:var(--accent);min-height:2rem;word-break:break-all;';
  msgBox.textContent = '—';

  const cmdRow = document.createElement('div');
  cmdRow.className = 'cmd-input';

  const cmdInput = document.createElement('input');
  cmdInput.id = 'cmd-' + safeId;
  cmdInput.placeholder = 'Command…';

  const cmdBtn = document.createElement('button');
  cmdBtn.textContent = 'Send';
  // Close over the original (unsanitized) clientId for topic construction,
  // but the DOM element ID lookup is done with the sanitized version.
  cmdBtn.addEventListener('click', () => sendCmd(clientId));

  cmdRow.appendChild(cmdInput);
  cmdRow.appendChild(cmdBtn);

  card.appendChild(header);
  card.appendChild(seenLine);
  card.appendChild(msgBox);
  card.appendChild(cmdRow);
  return card;
}

// MQTT connection
const mqttClient = mqtt.connect(BROKER_URL, {
  clientId: 'dashboard-' + Math.random().toString(16).slice(2, 8),
  clean: true,
  reconnectPeriod: 3000,
});

mqttClient.on('connect', () => {
  const el = document.getElementById('connection-status');
  el.textContent = '● Broker connected';
  el.style.color = 'var(--ok)';
  mqttClient.subscribe(TOPIC_PREFIX + '/+/telemetry');
  mqttClient.subscribe(TOPIC_PREFIX + '/+/status');
});

mqttClient.on('error', (err) => {
  const el = document.getElementById('connection-status');
  el.textContent = '● ' + sanitize(err.message);
  el.style.color = 'var(--err)';
});

mqttClient.on('offline', () => {
  const el = document.getElementById('connection-status');
  el.textContent = '● Disconnected';
  el.style.color = 'var(--err)';
});

mqttClient.on('message', (topic, payload) => {
  const parts = topic.split('/');
  if (parts.length < 3) return;
  const clientId = parts[1];
  const msg = payload.toString();

  const grid = document.getElementById('client-grid');
  if (!clients[clientId]) {
    // Remove empty-state placeholder on first device
    const empty = grid.querySelector('.empty-state');
    if (empty) empty.remove();

    const card = buildClientCard(clientId);
    grid.appendChild(card);
    clients[clientId] = true;
  }

  const safeId = sanitize(clientId);
  const msgEl = document.getElementById('msg-' + safeId);
  const seenEl = document.getElementById('seen-' + safeId);
  if (msgEl) msgEl.textContent = msg;
  if (seenEl) seenEl.textContent = new Date().toLocaleTimeString();
});

function sendCmd(clientId) {
  const safeId = sanitize(clientId);
  const input = document.getElementById('cmd-' + safeId);
  if (!input) return;
  const msg = input.value.trim();
  if (!msg) return;
  mqttClient.publish(TOPIC_PREFIX + '/' + clientId + '/cmd', msg);
  input.value = '';
}

function broadcast() {
  const input = document.getElementById('broadcast-msg');
  const msg = input.value.trim();
  if (!msg) return;
  Object.keys(clients).forEach(id => {
    mqttClient.publish(TOPIC_PREFIX + '/' + id + '/cmd', msg);
  });
  input.value = '';
}

// Load contact entries from the backend
async function loadContacts() {
  try {
    const r = await fetch('/api/contacts');
    const rows = await r.json();
    const tbody = document.getElementById('contacts-body');
    tbody.textContent = '';  // clear safely — no innerHTML

    if (!rows.length) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.colSpan = 5;
      td.style.color = 'var(--text-dim)';
      td.textContent = 'No submissions yet.';
      tr.appendChild(td);
      tbody.appendChild(tr);
      return;
    }

    rows.forEach(c => {
      const tr = document.createElement('tr');
      [c.name, c.email, c.company || '—', c.message || '—', c.created_at].forEach(val => {
        const td = document.createElement('td');
        td.textContent = val;  // textContent — safe even if DB was tampered
        tr.appendChild(td);
      });
      tbody.appendChild(tr);
    });
  } catch (e) {
    console.error('Failed to load contacts:', e);
  }
}

loadContacts();
