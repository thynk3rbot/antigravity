# Specification: PC Message Daemon & LMX Bridge
**Project:** LoRaLink Messenger (tools/loramsg)
**Status:** DESIGN APPROVED (2026-03-13)
**Ref:** ideas.txt Track 6

## 1. Core Architecture
The PC Daemon acts as a high-intelligence multiplexer between local UI (PWA), local hardware (Heltec via Serial/BLE), and remote daemons (Internet Bridge).

### 1.1 Transport Strategy (Multiplexing)
- **Current Idea:** BLE → Serial → WiFi → MQTT.
- **Recommendation:** Implement **Simultaneous Connection Monitoring**.
    - The daemon should maintain open handles to Serial and WiFi-REST simultaneously.
    - If a `STATUS` command fails on Serial due to a bus contention, the daemon should immediately retry via the WiFi IP address before reporting a transport failure to the user.

### 1.2 Message Persistence (SQLite)
- **Schema Recommendation:**
    - `messages`: id, src_id, dest_id, payload, timestamp, status (QUEUED, SENT, ACKED, FAILED).
    - `topology`: node_id, last_seen, last_latency, primary_transport, hop_count.
- **Eviction Policy:** Auto-prune messages older than 30 days to keep the daemon lightweight for older hardware.

## 2. LMX Protocol Enhancements

### 2.1 Routing & Discovery
- **Recommendation:** **Passive Beaconing.** Daemons should listen for `NODE_ANNOUNCE` packets and update the `topology` table.
- **Internet Bridge Optimization:** Peer daemons should exchange their "Known Nodes" list upon connection. This stops the daemon from trying to send a message over LoRa if it knows the destination is only currently reachable via a specific internet peer.

### 2.2 Fragmentation Logic (MsgType 0x8)
- **Problem:** LoRa payloads are tiny (max 225 bytes).
- **Recommendation:** Implement a **Sliding Window ARQ**. 
    - Divide large strings/files into blocks.
    - The receiver sends a single bitmask ACK for a group of blocks (e.g., "Received 1, 2, 4. Missing 3").
    - Dramatically reduces airtime compared to individual ACKs for every fragment.

## 3. OS Integration (Windows)

### 3.1 System Tray & Notifications
- **Tray States:**
    - 🔵 **Blue:** Full connectivity (Local + Bridge).
    - 🟡 **Yellow:** Degraded (Local only).
    - 🔴 **Red:** No hardware found.
- **Auto-Start:** provide a `--register-startup` CLI flag that adds the python entry-point to `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

### 3.2 Power-Miser Synchronization
- **Recommendation:** If the local Heltec reports it is entering **Hibernate** or **Long-Interval Sleep**, the PC Daemon should put its transport workers into **low-frequency polling mode** to save PC CPU cycles and prevent log spamming of failed connection attempts.

---
*Approved Architectural Assessment — Antigravity Protocol Suite*
