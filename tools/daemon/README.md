# LoRaLink PC Daemon

Central transport hub for LoRaLink device control.

## Features
- Multi-protocol transport (Serial, HTTP, BLE, LoRa, MQTT)
- Message persistence (SQLite)
- Device discovery & topology tracking
- REST API for clients (webapp, mobile, cloud)
- WebSocket for real-time events

## Architecture
- `daemon.py` — Main daemon service
- `api.py` — FastAPI REST/WebSocket endpoints
- `transport.py` — Transport abstraction layer
- `persistence.py` — SQLite message queue & state
- `models.py` — Data structures (Node, Message, Transport)
- `config.py` — Configuration management

## Running
```bash
python tools/daemon/daemon.py --config daemon.config.json
```
