// plugins/viai-site/static/main.js

document.addEventListener('DOMContentLoaded', () => {
    // Smooth scroll for internal links
    document.querySelectorAll('a[href^="#"]').forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const target = document.querySelector(link.getAttribute('href'));
            if (target) target.scrollIntoView({ behavior: 'smooth' });
        });
    });

    // Page-specific initialization
    if (document.getElementById('fleet-demo')) {
        loadFleetStatus();
        setInterval(loadFleetStatus, 15000); // Update every 15s
    }

    if (document.getElementById('search-form')) {
        const form = document.getElementById('search-form');
        form.addEventListener('submit', (e) => {
            e.preventDefault();
            const query = document.getElementById('search-query').value;
            if (query) searchDocs(query);
        });
    }
});

/**
 * Fetch and render live/mock fleet status.
 */
async function loadFleetStatus() {
    const container = document.getElementById('fleet-demo');
    if (!container) return;

    try {
        const resp = await fetch('/api/fleet-status');
        const data = await resp.json();
        
        container.innerHTML = ''; // Clear loading state
        
        if (data.devices) {
            data.devices.forEach(device => {
                const card = document.createElement('div');
                card.className = `device-card ${device.status.toLowerCase()}`;
                
                const title = document.createElement('strong');
                title.textContent = device.id;
                
                const info = document.createElement('p');
                info.textContent = `Status: ${device.status} | Battery: ${device.battery}% | Signal: ${device.signal} dBm`;
                
                card.appendChild(title);
                card.appendChild(info);
                container.appendChild(card);
            });
        }
    } catch (err) {
        container.textContent = 'Failed to load live fleet status.';
        console.error('Fleet Status Error:', err);
    }
}

/**
 * Handle Dify knowledge base search.
 */
async function searchDocs(query) {
    const resultsDiv = document.getElementById('search-results');
    if (!resultsDiv) return;

    resultsDiv.textContent = 'Searching knowledge base...';

    try {
        const resp = await fetch(`/api/rag/search?q=${encodeURIComponent(query)}`);
        if (!resp.ok) throw new Error('Search failed');
        
        const data = await resp.json();
        
        // Render result safely
        resultsDiv.innerHTML = '';
        const pre = document.createElement('pre');
        pre.style.whiteSpace = 'pre-wrap';
        pre.style.background = '#000';
        pre.style.padding = '1rem';
        pre.textContent = JSON.stringify(data, null, 2);
        resultsDiv.appendChild(pre);

    } catch (err) {
        resultsDiv.textContent = 'RAG Search is currently offline.';
        console.error('Search Error:', err);
    }
}
