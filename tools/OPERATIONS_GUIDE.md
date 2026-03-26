# Phase 50 Operations Guide

**Purpose:** Unified command center for managing Phase 50 fleet test, Ollama code generation, and hybrid model proxy

---

## Quick Start

### 1. Start Everything
```bash
tools/phase50_operations.bat start
# OR start the webapp
python tools/webapp/server.py --device <device-name>
```

Starts:
- ✅ Hybrid Model Proxy (local Ollama + OpenRouter routing)
- ✅ Checks Ollama availability
- ✅ Verifies daemon logs

### 2. Access Hybrid Proxy Dashboard
**Option A: Via Webapp (Recommended)**
```bash
python tools/webapp/server.py --device <device-name>
# Open http://localhost:8000
# Click: 🔗 Hybrid Proxy in sidebar
```

Shows:
- Proxy status (running/stopped)
- Ollama and OpenRouter health
- Request metrics and cost tracking
- Recent request history
- Start/stop controls
- OpenRouter API configuration

**Option B: Via Command Line**
```bash
tools/phase50_operations.bat dashboard
```

Shows:
- Device registrations in real-time
- Ollama task queue status
- Hybrid proxy request metrics
- Auto-refreshes every 10 seconds

### 3. Queue Phase 50.2 Code Generation
```bash
tools/phase50_operations.bat queue
```

Queues:
- Master Phase 50.2 task to local Ollama
- Full context + specification
- Generates ~1500-2000 lines of code

---

## Command Reference

### Control Panel
```bash
phase50_operations.bat              # Show menu
phase50_operations.bat start        # Start all services
phase50_operations.bat stop         # Stop all services
phase50_operations.bat status       # Check health of all services
phase50_operations.bat dashboard    # Live monitoring (30s refresh)
phase50_operations.bat queue        # Queue Phase 50.2 tasks
phase50_operations.bat monitor      # Tail fleet test logs
phase50_operations.bat logs         # Stream daemon logs
```

### Hybrid Proxy (Standalone)
```bash
hybrid_proxy.bat start              # Start proxy server
hybrid_proxy.bat stop               # Stop proxy server
hybrid_proxy.bat status             # Check Ollama + OpenRouter health
hybrid_proxy.bat test               # Run diagnostics
hybrid_proxy.bat report             # Show cost/metrics report
```

### Fleet Test (Standalone)
```bash
monitor_phase50.bat fleet           # Monitor device registrations
monitor_phase50.bat ollama          # Monitor Ollama queue
monitor_phase50.bat both            # Monitor everything
```

### Ollama Queue (Standalone)
```bash
ollama_queue.bat queue "model" "prompt"  # Queue custom task
ollama_queue.bat check                   # Show results
ollama_queue.bat process                 # Process queued tasks
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│         Phase 50 Operations Control Panel               │
│    (phase50_operations.bat)                              │
└──────────────┬──────────────────────────────────────────┘
               │
    ┌──────────┼──────────────┬─────────────────┐
    │          │              │                 │
    ↓          ↓              ↓                 ↓
┌─────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│ Daemon  │ │ Ollama   │ │ Hybrid   │ │ Monitoring   │
│ Fleet   │ │ Code Gen │ │ Proxy    │ │ Dashboard    │
│ Test    │ │ Queue    │ │ (router) │ │              │
└─────────┘ └──────────┘ └──────────┘ └──────────────┘
    │          │              │                 │
    ↓          ↓              ↓                 ↓
┌─────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│ MQTT    │ │ Local    │ │ OpenRouter│ │ Log Files    │
│ Devices │ │ Ollama   │ │ Cloud API │ │ Metrics      │
└─────────┘ └──────────┘ └──────────┘ └──────────────┘
```

---

## Workflow: Fleet Test + Code Generation (Parallel)

### Phase 1: Initialization (5 min)
```bash
# Terminal 1: Start daemon (in daemon directory)
python run.py --mqtt-broker localhost:1883 --log-level INFO > test_daemon.log 2>&1 &

# Terminal 2: Start Ollama app (Windows app or `ollama serve`)

# Terminal 3: Start all Phase 50 operations
tools/phase50_operations.bat start
```

**Verify:**
```bash
tools/phase50_operations.bat status
```

Expected output:
```
[Hybrid Proxy] ✓ RUNNING
  [Ollama] ✓ HEALTHY
  [OpenRouter] ✓ HEALTHY

[Ollama Queue] 0 tasks queued

[Daemon] Last registration: node-30 (aa:bb:cc:dd:ee:ff)
```

### Phase 2: Start Fleet Test (5 min)
```bash
# AG flashes devices on V2/V3/V4
# Devices boot, publish MQTT status

# Monitor:
tools/phase50_operations.bat dashboard
```

Watch for:
- Devices registering (should see `[Peer]` lines)
- First commands executing
- ACKs returning

### Phase 3: Queue Ollama Tasks (concurrent, 1 min)
```bash
# Once fleet test shows devices registered successfully:
tools/phase50_operations.bat queue
```

This queues:
- mesh_router.py (MAC-primary registry)
- mesh_api.py (API updates)
- test_phase_50_2_collisions.py (unit tests)
- test_phase_50_2_e2e.py (integration tests)
- firmware updates (control_packet.h, mqtt_transport.cpp)

**While this runs in background:**
```bash
# Monitor fleet test progress
tools/phase50_operations.bat monitor

# Or monitor Ollama progress
ollama_queue.bat check
```

### Phase 4: Results Review (30+ min later)
```bash
# Fleet test complete, Ollama code generated

# Check fleet test results
tools/phase50_operations.bat logs

# Check Ollama code generation
ollama_queue.bat check

# Show cost report
hybrid_proxy.bat report
```

**Expected:**
```
Fleet Test Results:
  - Devices registered: 3
  - Commands executed: 15
  - ACKs received: 15
  - Collisions: 0

Ollama Generation:
  - All 5 files generated: YES
  - Total tokens: ~2,450
  - Cost: $0.00 (FREE)

Hybrid Proxy Report:
  - Ollama requests: 5 (FREE)
  - OpenRouter requests: 0
  - Total cost: $0.00
```

---

## Integration with CI/CD

### Pre-Flight Check
```bash
tools/phase50_operations.bat status
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Phase 50 services not healthy
    exit /b 1
)
```

### Automated Testing
```bash
REM Start services
tools/phase50_operations.bat start

REM Run fleet test
call :run_fleet_test

REM Queue code generation
tools/phase50_operations.bat queue

REM Wait for completion
:wait_loop
timeout /t 30 /nobreak
ollama_queue.bat check | findstr "task_" >NUL
if %ERRORLEVEL% EQU 0 goto :wait_loop

REM Report results
hybrid_proxy.bat report
```

---

## Troubleshooting

### Proxy Won't Start
```bash
REM Check if already running
tasklist /FI "WINDOWTITLE eq Hybrid Model Proxy*"

REM Kill any orphaned processes
taskkill /FI "IMAGENAME eq python.exe" /T /F

REM Restart
hybrid_proxy.bat start
```

### Ollama Not Responding
```bash
REM Check if running
tasklist | findstr ollama

REM Restart Ollama app (Windows GUI)
REM Or: ollama serve (command line)

REM Verify health
hybrid_proxy.bat status
```

### No Fleet Test Logs
```bash
REM Daemon not running - start it:
cd daemon
python run.py --mqtt-broker localhost:1883 > ../test_daemon.log 2>&1 &

REM Verify with:
type test_daemon.log
```

### Ollama Queue Stuck
```bash
REM Check queue file
type %APPDATA%\LoRaLink\ollama_queue.txt

REM Manually process
ollama_queue.bat process

REM Or restart Ollama and reprocess
taskkill /FI "IMAGENAME eq ollama.exe"
ollama serve
ollama_queue.bat process
```

---

## File Organization

```
tools/
├── phase50_operations.bat          ← MAIN CONTROL PANEL (use this)
├── hybrid_proxy.bat                ← Proxy management
├── hybrid_model_proxy.py           ← Proxy implementation
├── ollama_queue.bat                ← Queue management (unchanged)
├── monitor_phase50.bat             ← Dashboard (unchanged)
├── queue_phase50_2_master.bat      ← Phase 50.2 task queue (unchanged)
└── OPERATIONS_GUIDE.md             ← This file

daemon/
├── run.py                          ← Launch daemon
├── test_daemon.log                 ← Logs (created at runtime)
└── src/
    ├── main.py
    ├── mesh_router.py
    └── mesh_api.py

%APPDATA%\LoRaLink\
├── ollama_queue.txt                ← Queued tasks
└── ollama_results/
    ├── task_1.txt
    ├── task_2.txt
    └── ...

%APPDATA%\.claude\hybrid_proxy\
├── proxy_20260326_020345.log       ← Proxy logs (timestamped)
└── ...
```

---

## Key Features

### 1. Unified Control
One command manages all Phase 50 operations:
```bash
phase50_operations.bat start        # Everything
phase50_operations.bat stop         # Everything
phase50_operations.bat status       # Everything
```

### 2. Live Monitoring
Real-time dashboard updates every 10 seconds:
```bash
phase50_operations.bat dashboard
```

Shows:
- Device registrations (MQTT)
- Ollama queue status
- Hybrid proxy metrics
- All in one view

### 3. Smart Routing
Hybrid proxy automatically:
- Checks Ollama health
- Routes to local (FREE) if available
- Falls back to OpenRouter if needed
- Tracks costs per request

### 4. Concurrent Execution
While fleet test runs, Ollama generates code:
- Device registration/testing: ~30 min
- Code generation: ~15 min
- Parallel = saves 15+ min of wall time

### 5. Cost Tracking
Every request logged with metrics:
```bash
hybrid_proxy.bat report
```

Shows:
- Ollama vs OpenRouter split
- Total tokens generated
- Cost per request
- Savings estimate

---

## Hybrid Proxy via Webapp

The **🔗 Hybrid Proxy** tab in the webapp provides a unified control interface:

### Status Panel
- **Proxy Server**: Shows if proxy is running/stopped
- **Ollama Health**: Checks local Ollama availability (http://localhost:11434)
- **OpenRouter Health**: Verifies cloud API connectivity
- **Uptime**: Shows how long proxy has been running

### Metrics Dashboard
- **Total Requests**: Cumulative request count today
- **Ollama Requests**: Count and cost ($0.00 local)
- **OpenRouter Requests**: Count and cost ($0.0003-0.075/1M tokens)
- **Total Cost**: Daily spending estimate

### Recent Requests Table
- **Time**: Request timestamp
- **Backend**: Which service handled the request (ollama/openrouter)
- **Model**: LLM model used
- **Tokens**: Prompt + completion tokens
- **Latency**: Response time in milliseconds
- **Cost**: USD cost for that request

### Controls
- **▶ Start / ⏹ Stop**: Toggle proxy server
- **Test**: Verify OpenRouter API connectivity
- **Save Configuration**: Persist Ollama endpoint and OpenRouter API key

### Configuration
- **Ollama Endpoint**: Local Ollama server (default: http://localhost:11434)
- **OpenRouter API Key**: Optional; enables fallback to cloud if local is down

---

## Best Practices

1. **Always check status before starting**
   ```bash
   phase50_operations.bat status
   # OR: Check 🔗 Hybrid Proxy tab in webapp
   ```

2. **Use webapp for real-time monitoring** (Recommended)
   ```bash
   python tools/webapp/server.py --device <device-name>
   # Open http://localhost:8000 → 🔗 Hybrid Proxy tab
   ```

3. **Queue Phase 50.2 only after fleet test starts successfully**
   ```bash
   # Wait for first device registration, then:
   phase50_operations.bat queue
   ```

4. **Save logs before restarting**
   ```bash
   REM Logs auto-saved, but copy if important:
   copy test_daemon.log test_daemon_backup_%date:~-2%%date:~-5,2%.log
   ```

5. **Review costs via webapp dashboard**
   - Open 🔗 Hybrid Proxy tab → Metrics section
   - Costs auto-update as requests complete

---

## Integration Summary

| Component | File | Purpose | Status | Access |
|-----------|------|---------|--------|--------|
| Control Panel | `phase50_operations.bat` | Master command center | ✅ Ready | Batch |
| Proxy Manager | `hybrid_proxy.bat` | Local/cloud routing | ✅ Ready | Batch |
| **Proxy Dashboard** | **Webapp** | **Hybrid proxy UI** | **✅ Ready** | **🔗 Via Webapp** |
| Proxy Engine | `hybrid_model_proxy.py` | Smart model router | ✅ Ready | Python |
| Fleet Monitor | `monitor_phase50.bat` | Dashboard | ✅ Ready | Batch |
| Task Queue | `ollama_queue.bat` | Code generation | ✅ Ready | Batch |
| Task Queueer | `queue_phase50_2_master.bat` | Phase 50.2 tasks | ✅ Ready | Batch |

---

**You're all set! The Phase 50 operations infrastructure is integrated and ready for use.** 🚀

Start with:
```bash
tools/phase50_operations.bat start
tools/phase50_operations.bat dashboard
```