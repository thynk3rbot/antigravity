/**
 * Magic Assistant - Core App Logic
 */

let currentSessionId = null;
let currentDomain = 'general';
let socket = null;

document.addEventListener('DOMContentLoaded', () => {
    initBranding();
    initHealthCheck();
    initDomains();
    initSessions();
    initChat();
    initWebSocket();
    initVoice();
    initToasts();
});

function initToasts() {
    const container = document.createElement('div');
    container.id = 'toast-container';
    document.body.appendChild(container);
}

function showToast(message, type = 'info') {
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    
    document.getElementById('toast-container').appendChild(toast);
    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateX(20px)';
        toast.style.transition = '0.3s';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

/**
 * Loads branding from API and applies to UI
 */
async function initBranding() {
    try {
        const response = await fetch('/api/branding');
        const config = await response.json();
        
        document.title = config.app_name || 'Assistant';
        const titleEl = document.querySelector('.app-title');
        if (titleEl) titleEl.textContent = config.app_name || 'Assistant';
        
        const taglineEl = document.querySelector('.app-tagline');
        if (taglineEl) taglineEl.textContent = config.tagline || '';
        
        const copyrightEl = document.querySelector('.copyright');
        if (copyrightEl) copyrightEl.textContent = config.copyright || '';
        
        if (config.accent_color) {
            document.documentElement.style.setProperty('--accent', config.accent_color);
            if (config.accent_color.startsWith('#')) {
                const hex = config.accent_color.replace('#', '');
                const r = parseInt(hex.substring(0, 2), 16);
                const g = parseInt(hex.substring(2, 4), 16);
                const b = parseInt(hex.substring(4, 6), 16);
                document.documentElement.style.setProperty('--accent-glow', `rgba(${r}, ${g}, ${b}, 0.4)`);
            }
        }
        
        if (config.icon_path) {
            const logo = document.querySelector('.app-logo');
            if (logo) {
                logo.src = config.icon_path;
                logo.onerror = () => logo.style.display = 'none';
            }
        }
    } catch (err) {
        console.error('Failed to load branding:', err);
    }
}

/**
 * Periodically checks server and Ollama health
 */
function initHealthCheck() {
    const ollamaDot = document.getElementById('ollama-dot');
    const serverDot = document.getElementById('server-dot');
    
    const check = async () => {
        try {
            const response = await fetch('/health');
            const status = await response.json();
            serverDot.className = 'status-dot dot-green';
            ollamaDot.className = status.ollama ? 'status-dot dot-green' : 'status-dot dot-red';
        } catch (err) {
            serverDot.className = 'status-dot dot-red';
            ollamaDot.className = 'status-dot dot-red';
        }
    };
    
    check();
    setInterval(check, 10000);
}

/**
 * Initializes WebSocket connection
 */
function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/chat`;
    
    socket = new WebSocket(wsUrl);
    
    socket.onopen = () => {
        console.log('WebSocket connected');
    };
    
    socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        
        if (data.type === 'token') {
            handleIncomingToken(data.content);
        } else if (data.type === 'done') {
            handleDone(data.metrics);
        } else if (data.type === 'error') {
            showToast(`AI Error: ${data.content}`, 'error');
            if (activeBotMsgId) {
                updateMessage(activeBotMsgId, `Error: ${data.content}`);
                activeBotMsgId = null;
            }
        }
    };
    
    socket.onclose = () => {
        console.warn('WebSocket closed. Reconnecting...');
        setTimeout(initWebSocket, 3000);
    };
}

let activeBotMsgId = null;

function handleIncomingToken(token) {
    if (!activeBotMsgId) {
        activeBotMsgId = appendMessage('bot', '');
    }
    const msgDiv = document.getElementById(activeBotMsgId);
    if (msgDiv) {
        const bubble = msgDiv.querySelector('.bubble');
        bubble.textContent += token;
        const area = document.getElementById('messages-area');
        area.scrollTop = area.scrollHeight;
    }
}

function handleDone(metrics) {
    if (activeBotMsgId) {
        updateMessage(activeBotMsgId, null, metrics);
        activeBotMsgId = null;
    }
}

/**
 * Fetches available domains and populates the selector
 */
async function initDomains() {
    const select = document.getElementById('domain-select');
    try {
        const resp = await fetch('/api/domains');
        const data = await resp.json();
        select.innerHTML = '<option value="general">General</option>';
        data.domains.forEach(d => {
            const opt = document.createElement('option');
            opt.value = d.id;
            opt.textContent = d.name;
            select.appendChild(opt);
        });
        select.addEventListener('change', (e) => {
            currentDomain = e.target.value;
        });
    } catch (err) {
        console.error('Failed to load domains:', err);
    }
}

/**
 * Fetches and displays recent chat sessions
 */
async function initSessions() {
    const list = document.getElementById('session-list');
    try {
        const resp = await fetch('/api/sessions');
        const data = await resp.json();
        list.innerHTML = '';
        data.sessions.forEach(s => {
            const item = document.createElement('div');
            item.className = 'session-item' + (s.id === currentSessionId ? ' active' : '');
            item.textContent = s.title || `Session ${s.id}`;
            item.dataset.id = s.id;
            item.addEventListener('click', () => loadSession(s.id));
            list.appendChild(item);
        });
    } catch (err) {
        console.error('Failed to load sessions:', err);
    }
}

async function loadSession(sid) {
    currentSessionId = sid;
    document.querySelectorAll('.session-item').forEach(i => {
        i.classList.toggle('active', i.dataset.id === sid);
    });
    const area = document.getElementById('messages-area');
    area.innerHTML = '';
    try {
        const resp = await fetch(`/api/sessions/${sid}`);
        const data = await resp.json();
        data.messages.forEach(m => appendMessage(m.role, m.content));
    } catch (err) {
        console.error('Failed to load history:', err);
    }
}

function initChat() {
    const chatInput = document.getElementById('chat-input');
    const sendBtn = document.getElementById('send-btn');
    
    const handleSend = async () => {
        const text = chatInput.value.trim();
        if (!text) return;
        
        if (!currentSessionId) {
            const title = text.substring(0, 20) + (text.length > 20 ? '...' : '');
            try {
                const resp = await fetch('/api/sessions', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ title, domain: currentDomain })
                });
                const data = await resp.json();
                currentSessionId = data.session_id;
                await initSessions();
            } catch (err) {
                console.error('Failed to create session:', err);
                return;
            }
        }
        
        appendMessage('user', text);
        chatInput.value = '';
        
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({
                type: 'chat',
                message: text,
                session_id: currentSessionId,
                domain: currentDomain
            }));
        } else {
            appendMessage('bot', 'Error: WebSocket is not connected.');
        }
    };
    
    sendBtn.addEventListener('click', handleSend);
    chatInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') handleSend();
    });
}

function appendMessage(role, text) {
    const area = document.getElementById('messages-area');
    const msgId = 'msg-' + Math.random().toString(36).substr(2, 9);
    const msgDiv = document.createElement('div');
    msgDiv.className = `message ${role}`;
    msgDiv.id = msgId;
    const bubble = document.createElement('div');
    bubble.className = 'bubble';
    bubble.textContent = text;
    msgDiv.appendChild(bubble);
    area.appendChild(msgDiv);
    area.scrollTop = area.scrollHeight;
    return msgId;
}

function updateMessage(msgId, text, metrics = null) {
    const msgDiv = document.getElementById(msgId);
    if (!msgDiv) return;
    const bubble = msgDiv.querySelector('.bubble');
    if (text !== null) bubble.textContent = text;
    if (metrics && metrics.model) {
        const stats = document.createElement('div');
        stats.className = 'sources';
        stats.style.marginTop = '8px';
        stats.innerHTML = `<span class="source-pill">${metrics.model} | ${metrics.total_duration ? (metrics.total_duration / 1e9).toFixed(1) + 's' : ''}</span>`;
        msgDiv.appendChild(stats);
    }
    const area = document.getElementById('messages-area');
    area.scrollTop = area.scrollHeight;
}

/**
 * Push-to-Talk (PTT) implementation using Web Speech API
 */
function initVoice() {
    const micBtn = document.getElementById('mic-btn');
    const chatInput = document.getElementById('chat-input');
    
    if (!('webkitSpeechRecognition' in window) && !('SpeechRecognition' in window)) {
        micBtn.style.display = 'none';
        return;
    }
    
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    const recognition = new SpeechRecognition();
    
    recognition.continuous = false;
    recognition.interimResults = true;
    recognition.lang = 'en-US';
    
    let isRecording = false;
    
    micBtn.addEventListener('click', () => {
        if (isRecording) {
            recognition.stop();
        } else {
            recognition.start();
        }
    });
    
    recognition.onstart = () => {
        isRecording = true;
        micBtn.classList.add('recording');
        micBtn.textContent = '🛑';
    };
    
    recognition.onresult = (event) => {
        let transcript = '';
        for (let i = event.resultIndex; i < event.results.length; i++) {
            transcript += event.results[i][0].transcript;
        }
        chatInput.value = transcript;
    };
    
    recognition.onerror = (event) => {
        console.error('Speech recognition error:', event.error);
        stopRecording();
    };
    
    recognition.onend = () => {
        stopRecording();
    };
    
    function stopRecording() {
        isRecording = false;
        micBtn.classList.remove('recording');
        micBtn.textContent = '🎤';
    }
}
