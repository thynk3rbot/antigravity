# LoRaLink Feature Ledger (Chronological Build History)

---
status: complete
owner: antigravity
---

This document tracks the consolidated feature set of the LoRaLink-AnyToAny 
firmware from project origin, maintained from a build perspective.

## 1. Primary History (The v1.x Development Track)

### v1.0 - November 2025 (Initial Release)
- Base LoRa mesh communication protocol.
- Serial command interface (CommandManager v1).
- Basic GPIO control (Standard Relay logic).
- Target Board: Heltec WiFi LoRa 32 V3 (ESP32-S3).

### v1.2 - December 2025 (Security and Web)
- AES-128 Encryption implemented for LoRa payloads.
- Integrated Web Dashboard (WiFiManager) for live monitoring.
- First-generation REST API (/api/status, /api/cmd).
- OTA Firmware updates enabled via WiFi.

### v1.3 - January 2026 (Reliability and Peering)
- ESP-NOW Support: High-speed peer-to-peer side channel.
- Peer Registry: Tracking remote nodes via NVS.
- ACK + Retry System: Reliable delivery for mission-critical commands.
- Dynamic Task Scheduler: JSON-based runtime task configuration.

### v1.4 - February 2026 (Refinement and Tools)
- Web Dashboard v2: SCADA-density layout and tool integration.
- Pin Scheduling GUI: Visual management of relay tasks.
- Config Export/Import: Porting settings between node hardware.
- Subnet Scanning: Automatic discovery of WiFi peers.
- Protocol Verification: Introduction of analytical testing tools.

### v1.5 - March 2026 (Infrastructure Integration)
- MQTT Bridge: Connecting the mesh to external brokers (EMQX).
- Unified Startup Orchestration: Service-level control for tools.
- BLE Scan Auto-Refresh: Real-time UI updates for nearby peers.
- Safari/iOS Compatibility: Major CSS overhaul for mobile devices.

---

## 2. The Fleet Synchronization Point (The v0.x Baseline)

### v0.1.0 - March 2026 (Current Stable Baseline)
- ARCHITECTURAL RESET: Consolidation of all v1.5 features into a stable baseline.
- FLEET LOCK: Version synchronized across all Heltec units (Peer1/Peer2).
- Range Boost: Variable-sized packets and Time-on-Air (ToA) optimizations.
- Power-Miser Tier 1: Voltage-aware throttling and peripheral gating.
- Unified Deployment: Introduction of deploy_dual.ps1 for fleet parity.
- Auto-Increment Disabled: Versioning moved to manual bump to prevent drift.

---

## 3. Projected Builds (Roadmap)

### v0.2.0 - Target: Late March 2026
- Wake-on-Radio (CAD): Pulse-based low power radio RX.
- Branded Subdomain Routing: DNS migration for permanent cloud hooks.
- Cloudflare Access SSO: Secure authentication for external cockpit management.

---

Build Principle: 
Stability over Drift. Features are built on top of the v0.1.0 synchronized 
baseline to ensure mission-critical reliability across the entire fleet.
