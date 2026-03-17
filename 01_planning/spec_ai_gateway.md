# Specification: Sovereign Intelligence & Swarm Orchestration

**Project:** LORALINK-UNIFIED (Local AI Gateway + PC Daemon)
**Status:** DRAFTING
**Ref:** [LL-AI-01]

## 1. The Local AI Gateway (The Cerebral Core)

The Local AI Gateway is a standalone service (or Docker container) that provides a mission-aware intelligence layer for the LoRaLink fleet.

### 1.1 Core Functions

- **Local Inference**: Interface with Local LLMs (Ollama/LocalAI) to process telemetry and commands without internet dependency.
- **Mission Context Provider**: Maintained vector store of current fleet state, battery levels, and sensor history.
- **Agentic Loop**: Autonomous monitoring that triggers specific PC Daemon routes based on AI-evaluated conditions (e.g., "Battery trend shows critical failure in 6 hours, initiate swarm conserve mode").

### 1.2 Integration via PC Daemon

- The AI Gateway communicates with the **PC Daemon via optimized IPC (Inter-Process Communication)**.
- It uses the Daemon's `ll://` URI addressing to query individual nodes or broadcast to swarms.

## 2. Swarm Orchestration (Collective Intelligence)

Moving beyond "Point-to-Point" to "Collective" behavior.

### 2.1 Swarm States

- **Sovereign Mesh**: Autonomous routing where nodes negotiate the "Fall-back Ladder" based on collective health.
- **Distributed Compute**: Offloading "heavy" firmware logic (e.g., complex FFT analysis) to the AI Gateway, returning only the actionable result to the LoRa mesh.
- **Heartbeat Synchronization**: Aligning wake-intervals across multiple nodes to maximize CAD (Wake-on-Radio) efficiency.

### 2.2 Orchestration Logic

- **Master/Member Election**: Automatic promotion of the "Active Gateway" node based on superior signal quality to the PC Daemon.
- **Consensus Triggers**: Requiring ACK from 2+ nodes before critical agricultural actions (e.g., opening a flood valve).

## 3. Remote Device Programming (Tunneling)

Seamless firmware management through the PC Daemon.

### 3.1 Serial-to-LoRa Tunneling

- The PC Daemon provides a virtual COM port or Socket that tunnels ESP32 flashing protocols over the most reliable link (Wifi > LoRa).
- **Differential OTA**: Sending only binary diffs via LMX fragments to reduce airtime for remote programming.

### 3.2 Specific Device Lock

- Capability to "pin" the AI Gateway's focus to a single NodeID for deep diagnostics without interrupting the rest of the swarm's traffic.

---

### Drafting Phase — Antigravity Protocol Suite
