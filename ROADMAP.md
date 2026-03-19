# LoRaLink Project Roadmap

This document outlines the planned growth and development tracks for the LoRaLink system, including hardware, firmware, and the supporting cloud/web infrastructure.

---

## 🛤️ Track 1: Core System & Firmware (Any-to-Any)

Focus: Improving reliability, protocol support, and the embedded core.

### Short-Term (Current Phase)

- [x] **v1.6.0 Release**: Range Boost (ToA optimization), Reliable Binary Transport (ACK/Retries), and Power-Miser integration.
- [x] **Range Boost Protocol**: Implemented binary command support and variable-size packets (40% ToA optimization).
- [x] **Reliable Binary Transport**: Added `BC_ACK` and automatic retry logic (3 tries) for performance-critical commands.
- [x] **Protocol Failover Algorithm**: Implemented automatic fallback to Legacy LoRa Text for mission-critical pings (`FPING`) when binary transport fails.
- [x] **Failover Monitoring Task**: Added `FAILOVER_PING` task type to scheduler and web UI for continuous reliability tracking.
- [x] **Config Pending Timeout**: Config settings panel now times out stuck `⏱ Pending` states after 10s and marks them `? Unconfirmed` instead of hanging forever. Polling continues every 2s until confirmed.
- [x] **Auto IP Rediscovery**: TransportManager now automatically scans the /24 subnet after 5 consecutive HTTP failures, recovering device connection after DHCP-assigned IP changes on reboot.
- [x] **Node Roster Clear All**: Added prominent `✕ Clear All` button to the Configured Nodes panel header for bulk cleanup of redundant/stale node entries.
- [x] **Performance Monitor Panel**: Live metric cards (Free Heap, Loop Avg, Loop Peak, Battery, WiFi RSSI) with 2-minute sparkline history and threshold alerting, plus counters row (uptime, ESP-NOW TX/RX, peers, resets, LoRa ToA). Webapp-only — zero firmware changes required. Data decimated from 4 Hz WS feed to ~1 sample/sec; history persists across page refresh via sessionStorage.
- [x] **Maintainability Audit**: Resolved 50+ inline CSS warnings in `index.html` via a utility-class system and `@supports` hooks.
- [ ] **WiFi Persistence Fix**: Resolve DataManager issue where WiFi credentials don't save to NVS on some boards.
- [ ] **Serial Flow Control**: Fix RX/TX imbalance on MASTER node cables.

### Track 4: Power-Miser (Smart Agriculture)

Focus: Ultra-low-power autonomous operation for solar-field units.

- [x] **Voltage-Aware Intelligence**: Multi-tier throttling with hysteresis (Normal/Conserve/Critical). 50mV deadband prevents oscillation.
- [x] **Safe-Shutdown (Critical Mode)**: Proactive WiFi kill, LORA downgrade, and link lockout when battery hits <3.45V.
- [x] **Modem-Sleep (Conserve Mode)**: Automatic 50% current reduction via WiFi PowerSave (Modem Sleep) when battery is <3.80V.
- [x] **Peripheral Gating**: `VEXT_CTRL` dynamically managed — rail OFF in CONSERVE/CRITICAL, ON in NORMAL.
- [x] **Solar Trend Analysis**: Ring buffer tracks charge/drain velocity (mV/min) and predicts "Time to Empty" (hours).
- [x] **BLE Power Gating**: BLE task processing skipped entirely in CRITICAL mode.
- [ ] **Wake-on-Radio (CAD)**: Pulse radio in low-power detection mode instead of constant RX.
- [ ] **NVS Hibernate**: Preserve state across multi-day deep-sleep cycles.

### Mid-Term

- [ ] **Smart Agriculture Expansion**: Optimized deep-sleep cycles for solar-only operation (Target: 6 months per charge).
- [ ] **GPS Integration**: Dynamic node mapping in the Fleet Admin via NMEA serial/I2C modules.
- [ ] **Binary Protocol Optimizations**: Transitioning from JSON-over-LoRa to a packed binary format for 40% range increase.

---

## 🌍 Track 2: Infrastructure & Web Hosting (The "Cloud Setup")

**Note: This is a separate infrastructure project to enable secure, branded external access.**

### Phase 1: Temporary Demo Access (Current)

- [x] **Cloudflare Quick Tunnels**: Implemented `Start_Public_Demo_Cloudflare.bat` for instant `.trycloudflare.com` URLs.
- [x] **LocalTunnel Fallback**: Implemented `Start_Public_Demo.bat` as an alternative.
- [x] **Master Launch Control**: Unified Docker engine health-checks and service orchestration into `Start_Loralink.bat`.

### Phase 2: Branded Zero-Trust Deployment (Target: Next 30 Days)

- [x] **Windows Reboot Survival**: Implemented `Register_Startup_Task.bat` for automatic ecosystem launch on Windows login.
- [x] **Process Crash Survival**: Added 5-second automatic restart loops to all background server scripts.
- [ ] **Cloudflare DNS Migration**: Move `spw1.com` and `viai.club` nameservers to Cloudflare.
- [ ] **Permanent Tunnels**: Configure `cloudflared` as a Windows service on the dev server.
- [ ] **Subdomain Routing**:
  - `app.spw1.com` ➔ Fleet Admin (8000)
  - `docs.spw1.com` ➔ Documentation (8001)
  - `web.spw1.com` ➔ Marketing Site (8010)
- [ ] **Single Sign-On (SSO)**: Implement Cloudflare Access (GitHub/Google login) in front of the Fleet Admin for secure external management.

### Phase 3: High-Availability Hosting

- [x] **Dockerized Production Stack**: Created `docker-compose.production.yml` with `restart: always` policies.
- [x] **Distribution Packager**: Updated `Package_Release.bat` to bundle production-ready entry points and operations docs.
- [ ] **Off-site Backup**: Auto-sync local `LOGS/` and `REPORTS/` to a secure cloud bucket (S3/Cloudflare R2).

---

## 📈 Track 3: Marketing & Commercialization

Focus: Growing the user base and establishing the brand.

### Current Goals

- [ ] **Launch "Connected Where the Map Ends" Campaign**: Using the documentation site as a lead magnet.
- [ ] **GitHub Registry Submission**: Submit the PlatformIO library to the official registry.
- [ ] **Smart Ag Case Study**: Document the 110V/12V relay usage in a real field deployment.

---

## 🏗️ Track 5: Unified Architecture (Industrial-Strength)

Focus: LORALINK-UNIFIED "Architectural Anchors" for protocol-agnostic, peer-sovereign operations.

- [ ] **Transport-Agnostic Envelope (LL-CORE-01)**: Implement HMAC-SHA256 signature verification at the gateway/router level before logic execution.
- [ ] **The Fallback Ladder (LL-CORE-02)**: Intelligent radio stack pruning (Hardwire > WiFi > LoRa) to conserve power once a secure link is established.
- [ ] **Semantic URI Addressing (LL-CORE-03)**: Transition to hardware-agnostic C2 via `ll://[node_id]/[subsystem]/[target]`.
- [ ] **Ranch-Ready Lexicon (LL-C2-01)**: Standardize all human-readable serial/MQTT/LoRa commands for parity in manual overrides.

---

## 🧠 Track 6: Sovereign Intelligence & Swarms

Focus: Local AI Gateway integration and collective fleet behavior (Swarms).

- [ ] **Local AI Gateway (LL-AI-01)**: Deploy local inference bridge (Ollama/LocalAI) for offline telemetry analysis.
- [ ] **Swarm Orchestration (LL-AI-02)**: Implement "Master/Member" election and consensus-based command execution via PC Daemon.
- [ ] **Remote Device Programming**: Enable Serial-to-LoRa tunneling for remote OTA and specific device "lock-on" diagnostics.

---

### Last Updated: 2026-03-17
