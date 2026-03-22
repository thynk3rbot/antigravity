const chatContainer = document.getElementById('chat-container');
const messagesDiv = document.getElementById('messages');
const chatInput = document.getElementById('chat-input');
const sendBtn = document.getElementById('send-btn');
const overlay = document.getElementById('magic-overlay');

// Add a message to the UI
function addMessage(text, role = 'bot') {
    const msgWrapper = document.createElement('div');
    msgWrapper.className = `message ${role}`;
    
    const bubble = document.createElement('div');
    bubble.className = 'bubble glass';
    bubble.textContent = text;
    
    msgWrapper.appendChild(bubble);
    messagesDiv.appendChild(msgWrapper);
    
    // Smooth scroll to bottom
    chatContainer.scrollTo({
        top: chatContainer.scrollHeight,
        behavior: 'smooth'
    });
}

// Handle sending a message
async function handleSend() {
    const text = chatInput.value.trim();
    if (!text) return;

    chatInput.value = '';
    addMessage(text, 'user');
    
    // Show Magic Loading
    // overlay.classList.remove('hidden');

    try {
        // In this PoC, we send directly to our bridge
        const response = await fetch('/api/msg', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ message: text, user: 'PWA_Client' })
        });
        
        const data = await response.json();
        addMessage(data.response || 'Magic processed your request.');
    } catch (err) {
        console.error(err);
        addMessage('⚠️ Magic encountered an error. Is the bridge running?', 'bot');
    } finally {
        // overlay.classList.add('hidden');
    }
}

// Event Listeners
sendBtn.addEventListener('click', handleSend);
chatInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') handleSend();
});

// Auto-refresh status (Mock for PoC)
async function refreshStatus() {
    try {
        const resp = await fetch('/api/health');
        const data = await resp.json();
        const statusText = document.querySelector('.status-indicator .text');
        if (data.status) {
            statusText.textContent = 'System Connected';
            statusText.style.color = '#00ffaa';
        }
    } catch (e) {
        document.querySelector('.status-indicator .text').textContent = 'Link Offline';
        document.querySelector('.status-indicator .text').style.color = '#ff4444';
    }
}

setInterval(refreshStatus, 5000);
refreshStatus();
