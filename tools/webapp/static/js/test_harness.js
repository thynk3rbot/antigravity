/**
 * LoRaLink Test Harness - Message Transformation Trace
 */

async function runTransformationTrace() {
    const cmdInput = document.getElementById('th-cmd');
    const nodeSelect = document.getElementById('th-node');
    const resultsArea = document.getElementById('th-results');
    
    const cmd = cmdInput.value.trim();
    const nodeId = nodeSelect.value;
    
    if (!cmd) {
        if (window.showToast) showToast("Please enter a command to trace", "error");
        else alert("Please enter a command to trace");
        return;
    }

    resultsArea.style.display = 'block';
    document.getElementById('trace-source').innerText = cmd;
    document.getElementById('trace-transport').innerText = "Processing...";
    document.getElementById('trace-format').innerText = "";
    document.getElementById('trace-wire').innerText = "";
    document.getElementById('trace-fw-report').innerText = "Waiting for firmware response...";

    try {
        const response = await fetch('/api/test/transform', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ cmd, node_id: nodeId })
        });
        
        const data = await response.json();
        
        if (data.ok) {
            const trace = data.local_trace;
            document.getElementById('trace-transport').innerText = "Transport: " + trace.transport;
            document.getElementById('trace-format').innerText = "Mode: " + trace.tp_mode;
            document.getElementById('trace-wire').innerText = trace.wire_format;
            
            document.getElementById('trace-fw-report').innerHTML = `<i class="fas fa-spinner fa-spin"></i> Triggered on ${nodeId || 'Local'}. Awaiting parse report...`;
        } else {
            document.getElementById('trace-transport').innerText = "Error";
            document.getElementById('trace-wire').innerText = data.error || "Unknown error";
        }
    } catch (e) {
        console.error("Trace failed:", e);
        if (window.showToast) showToast("Trace failed: " + e.message, "error");
    }
}

/**
 * Hook to be called from main app.js message handler
 */
function handleTransformMessage(data) {
    if (data.type === 'serial_log' && data.text.includes('"ok":true') && data.text.includes('"format"')) {
        try {
            const match = data.text.match(/\{.*\}/);
            if (match) {
                const report = JSON.parse(match[0]);
                if (report.format && report.cmd) {
                    const fwReport = document.getElementById('trace-fw-report');
                    if (!fwReport) return;
                    
                    fwReport.innerHTML = `
                        <div class="mt-1"><span class="label-caps">Format:</span> <span class="text-accent">${report.format}</span></div>
                        <div class="mt-1"><span class="label-caps">Command:</span> <span class="text-ok">${report.cmd}</span></div>
                        <div class="mt-1"><span class="label-caps">Arguments:</span> <span class="text-dim font-mono">${report.args || '(none)'}</span></div>
                    `;
                    
                    // Highlight the firmware step as completed
                    fwReport.parentElement.parentElement.classList.add('completed');
                }
            }
        } catch (e) {
            console.error("Failed to parse transform report:", e);
        }
    }
}

async function refreshTestNodes() {
    const select = document.getElementById('th-node');
    if (!select) return;

    try {
        const r = await fetch('/api/nodes');
        const data = await r.json();
        const current = select.value;
        
        // Keep "Broadcast" as first option
        select.innerHTML = '<option value="">Broadcast (Local)</option>';
        if (data.nodes) {
            data.nodes.forEach(n => {
                const opt = document.createElement('option');
                opt.value = n.id;
                opt.innerText = `${n.name} (${n.type})`;
                if (n.id === current) opt.selected = true;
                select.appendChild(opt);
            });
        }
    } catch (e) {}
}

// Initial node load
refreshTestNodes();
setInterval(refreshTestNodes, 5000);
