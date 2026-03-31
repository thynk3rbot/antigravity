# Magic — Claude Orientation Guide

Welcome to the Magic project. This document provides the definitive technical worldview for agents managing the development, deployment, and operation of our autonomous mesh fleet.

## 📡 The Product: Magic v2
Magic is an **Autonomous Any2Any Mesh** network built on the Heltec ESP32-S3 platform. It prioritizes decentralized sovereignty—every node is a peer, a relay, and a local agent.

### Key Architecture: Phase 50
- **Autonomous Sovereignty**: Nodes make relay and GPIO decisions locally based on mesh-wide state.
- **MAC-Seeded Crypto**: Decentralized security using hardware MAC addresses as key seeds.
- **Tiered Telemetry**:
  - `STATUS`: Human-friendly JSON (name, bat_pct, rssi, basic gps).
  - `VSTATUS`: Technical forensic JSON (snr, heap, mac, hw_id, reset reason).
- **Transport Modes**: Any2Any switching between LoRa, WiFi, and BLE (controlled by `CommandManager`).

## 🤖 The Three-Agent Model (`docs/AGENT_RADIO.md`)
We operate as a coordinated unit without blocking:
- **Claude (Reasoning/Strategy)**: Handles daemon architecture, high-level planning, and high-quality logic.
- **Antigravity (Hardware/Validation)**: Real hardware testing, firmware stability (I2C hardening, mutexes), and deployment.
- **Local Model (Execution)**: 24/7 async generation of boilerplate, CSS, and repetitive patterns.

## 🛠️ Fleet Operations & Stability
- **Stability Baseline (v0.0.14)**: Mutex-protected I2C bus (`g_i2cMutex`) and hardened BootSequence timeouts.
- **Deployment**: USB is the mandatory path for bootstrapping; WiFi-OTA is for fleet-wide sync.
- **Baud Rate**: Always use **921600** for USB uploads to ensure ~15s cycle times.

## 📂 Documentation Structure
- `docs/AGENT_RADIO.md`: The inter-agent coordination protocol.
- `docs/V2_STABILITY_TEST_PLAN.md`: The roadmap for fleet stabilization.
- `docs/PHASE_50_*.md`: Design and implementation specs for the autonomous mesh.
- `docs/COMMAND_INDEX.md`: Definitive list of firmware commands.

---
**Motto:** Product ready. Agentic development. Full speed ahead.
