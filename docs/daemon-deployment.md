# LoRaLink PC Daemon — Deployment Guide

The PC Daemon is a lightweight background service that acts as the transport hub between
clients (webapp, phone, cloud) and the ESP32 device swarm.

---

## Architecture

```
Phone (BLE/WiFi/Internet)  ─┐
Webapp (localhost:8000)    ─┼─→  PC Daemon (:8001)  →  ESP32 Swarm
Cloud / Remote Client      ─┘         ↕
                                  SQLite Queue
```

---

## Quick Start

### Prerequisites

```bash
pip install -r tools/requirements.txt
# Required: fastapi uvicorn aiohttp
```

### Run directly (development)

```bash
python -m tools.daemon.daemon
# Or with custom config:
python -m tools.daemon.daemon --config /path/to/daemon.config.json
```

Daemon starts on `http://localhost:8001` by default. Verify:

```bash
curl http://localhost:8001/health
# {"status":"ok","nodes_count":0,"node_ids":[]}
```

---

## Configuration

Create `daemon.config.json` alongside your launch script:

```json
{
  "host": "0.0.0.0",
  "port": 8001,
  "db_path": "daemon_queue.db",
  "lifecycle_mode": "service",
  "start_on_boot": true
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `host` | `0.0.0.0` | Bind address. Use `127.0.0.1` to restrict to localhost only. |
| `port` | `8001` | REST API port. Webapp connects here. |
| `db_path` | `daemon_queue.db` | SQLite database file path (absolute or relative to CWD). |
| `lifecycle_mode` | `service` | `service` = run until killed. `session` = exit when webapp closes. |
| `start_on_boot` | `true` | Hint for system service installers. |

---

## Windows: Run as Background Service (NSSM)

[NSSM](https://nssm.cc) (Non-Sucking Service Manager) installs any executable as a Windows service.

### Step 1: Install NSSM

Download from https://nssm.cc/download and place `nssm.exe` in `C:\Windows\System32\` (or any PATH location).

### Step 2: Install the service

```powershell
# Run as Administrator
nssm install LoRaLinkDaemon "C:\Python311\python.exe"
nssm set LoRaLinkDaemon AppParameters "-m tools.daemon.daemon --config C:\LoRaLink\daemon.config.json"
nssm set LoRaLinkDaemon AppDirectory "C:\Users\<you>\Documents\Code\Antigravity"
nssm set LoRaLinkDaemon DisplayName "LoRaLink PC Daemon"
nssm set LoRaLinkDaemon Description "Transport hub for LoRaLink ESP32 swarm"
nssm set LoRaLinkDaemon Start SERVICE_AUTO_START
nssm set LoRaLinkDaemon AppStdout "C:\LoRaLink\daemon.log"
nssm set LoRaLinkDaemon AppStderr "C:\LoRaLink\daemon-error.log"
```

### Step 3: Start and verify

```powershell
nssm start LoRaLinkDaemon
curl http://localhost:8001/health
```

### Manage the service

```powershell
nssm stop LoRaLinkDaemon
nssm restart LoRaLinkDaemon
nssm remove LoRaLinkDaemon confirm
```

---

## Linux: Run as systemd Service

### Step 1: Create the service unit

```bash
sudo nano /etc/systemd/system/loralink-daemon.service
```

```ini
[Unit]
Description=LoRaLink PC Daemon
After=network.target

[Service]
Type=simple
User=<your-user>
WorkingDirectory=/home/<your-user>/Code/Antigravity
ExecStart=/usr/bin/python3 -m tools.daemon.daemon --config /home/<your-user>/Code/Antigravity/daemon.config.json
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### Step 2: Enable and start

```bash
sudo systemctl daemon-reload
sudo systemctl enable loralink-daemon
sudo systemctl start loralink-daemon
sudo systemctl status loralink-daemon
```

### Step 3: View logs

```bash
journalctl -u loralink-daemon -f
```

---

## Connecting Clients

### Webapp (same machine)

The webapp auto-connects to `http://localhost:8001`. No configuration needed — just start daemon before webapp:

```bash
# Terminal 1
python -m tools.daemon.daemon

# Terminal 2
python tools/webapp/server.py
```

### Phone on same WiFi

Point phone browser at `http://<your-pc-ip>:8001` — or use the webapp at `:8000` which proxies to the daemon.

To find your PC IP: `ipconfig` (Windows) or `ip addr` (Linux).

### Remote / Internet access

Expose the daemon via:
- **Tailscale** (recommended) — VPN mesh, no port forwarding
- **ngrok** — Quick tunnel: `ngrok http 8001`
- **Port forwarding** — Forward port 8001 on your router (set `host: 0.0.0.0` in config)

---

## REST API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Daemon liveness check |
| POST | `/api/nodes` | Register a device node |
| GET | `/api/nodes` | List all nodes |
| GET | `/api/nodes/{id}` | Get node by ID |
| DELETE | `/api/nodes/{id}` | Remove node |
| POST | `/api/command` | Send command to device |
| GET | `/api/messages` | List message history |
| GET | `/api/messages/{id}` | Get message by ID |

Full interactive docs at `http://localhost:8001/docs` (FastAPI Swagger UI).

---

## Troubleshooting

### Daemon won't start — `ModuleNotFoundError`

Ensure you're running from the repo root and have activated your virtual environment.

```bash
cd /path/to/Antigravity
pip install -r tools/requirements.txt
python -m tools.daemon.daemon
```

### Port 8001 already in use

```bash
# Find what's using it:
netstat -ano | findstr :8001      # Windows
lsof -i :8001                     # Linux/Mac
# Change port in daemon.config.json
```

### Webapp shows "Daemon: disconnected"

1. Is daemon running? `curl http://localhost:8001/health`
2. Is port 8001 accessible? Check firewall rules.
3. Check daemon log for errors.

### Command returns FAILED (not SENT)

Transport handlers are stubs in current version — actual device delivery
(real HTTP/BLE/Serial probing) is marked TODO in `transport.py`. Commands
are persisted and routed but delivery confirmation requires hardware connection.

### SQLite database locked

Only one daemon instance should run at a time. Stop any existing daemon:

```bash
nssm stop LoRaLinkDaemon    # Windows
systemctl stop loralink-daemon  # Linux
```

---

## Development: Running Tests

```bash
# Unit tests
pytest tests/daemon/ -v

# Integration tests
pytest tests/integration/ -v

# All tests
pytest tests/ -v
```

Expected: 22 tests pass.
