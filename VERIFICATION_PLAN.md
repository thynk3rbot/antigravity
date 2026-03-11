
# LoRaLink v1.6.0 Verification & Next-Steps Plan

This document outlines the validation steps for the **Range Boost (Binary Mode)** and **Power-Miser (Smart Ag)** features before the next firmware flash.

## 📦 Release v1.6.0 Components

- **Current Baseline (Verified 2026-03-11):**
  - **Master:** v1.6.37 (IP: 172.16.0.27) -> **PASS (11/11)**
  - **Slave:** v1.6.34 (IP: 172.16.0.26) -> **PASS (11/11)**

1. **Range Boost Core:** Implementation of variable-sized physical packets (ToA reduction).
2. **Binary Protocol Layer:** Byte-oriented command structure (`0xAA` token) for 40% bandwidth efficiency.
3. **Reliable Binary Transport:** Automatic `BC_ACK` and retry logic (3 tries) for core commands.
4. **Power-Miser Engine:** Multi-tier voltage throttling (Normal/Conserve/Critical).
5. **Peripheral Gating:** Physical `VEXT` rail management for Heltec V3.

---

## ✅ Phase 1: Pre-Flash Verification

We will verify the logic using the following manual tests:

### 1.1 Range Boost (Time-on-Air)

- **Command:** `RTEST`
- **Verification:** The device will send one Legacy (92-byte) and one Boosted (variable) packet.
- **Expected Result:** Serial output should show a significant reduction in `LoRa ToA`.
  - Legacy (fixed): ~327ms (at SF10/250kHz)
  - Boosted (short "PING"): <180ms

### 1.2 Power-Miser Intelligence

- **Command:** `PMISER CRITICAL`
- **Verification:**
  - `DASHBOARD` card should turn **RED**.
  - `OLED` should be forced **OFF**.
  - `Heartbeat` interval in `SCHED` report should jump to **3600s**.
- **Command:** `PMISER NORMAL`
  - System should restore and reactivate the screen.

### 1.3 Reliable Binary Verification

- **Command:** `BINCMD 6 1` (Binary Ping)
- **Verification:** 
  - Source should log "`LORA: Queued reliable BINARY`".
  - Destination should return "`BIN: ACK token 0x06`".
  - Source should log "`LORA: BINARY ACK Verified`".

---

## ⚡ Gaps & "Tough Questions" Analysis (Skeptical Mode)

Current testing coverage is strong on **Gateway-to-PC** availability, but reveals the following vulnerabilities:

1. **Blind Mesh Routing (FIXED)**: 
   - **Risk**: The regression suite previously marked Mesh commands (`M1 STATUS`) as **PASS** even if the node was offline.
   - **Resolution (2026-03-11)**: Implemented "Response-Aware Validation" in `nightly_test.py`. The executor now polls the Gateway's `/api/status` for the specific `[NodeID] Response` string.
   - **Verification**: `Skeptical_Retry` test against `FakeNode` correctly timed out (FAIL), proving the suite now detects mesh-routing failures.

2. **Binary Transport Verification**:
   - `BINCMD` paths are not yet exercised in the nightly suite. We are only verifying the text-based CLI bridges.
   - **Edge Case**: What happens if the binary token `0xAA` is corrupted? The failover fallback needs an automated "Noise Simulation" test.

3. **Power-Miser Edge Cases**:
   - We haven't verified behavior during "Oscillating Voltage" (Brownout simulator).
   - **Request**: Can we automate a voltage sweep through the `M3 (Slave)` ADC simulation path?

## 🧪 Phase 3: "Empty-Config" Discovery Test

To verify the **Any-To-Any** discovery and webapp auto-population logic:

1. **Setup**: Use `pio run -t erase` or the PRG-button factory reset to wipe both Master and Slave.
2. **Flash**: Deploy the same "empty" v1.6.0 binary to both units.
3. **App Start**: Launch the `Fleet Admin` webapp and initiate a `/api/discover` scan.
4. **Goal**: Verify that the Slave node (unconfigured) is correctly discovered via LoRa/mDNS and populates the Node Registry with its default MAC-suffix identity automatically.

---

## 📂 Source Management Tracking
- [x] **v1.6.0 Baseline Committed**: `feature/lora-traffic-optimization`
- [x] **Nightly Test Engine Integrated**: `tools/testing/engine.py`
- [x] **Response-Aware Mesh Validation Added**: `tools/nightly_test.py`
- [x] **Registry UUID Mapping Fixed**: `server.py`

### 2.1 Wake-on-Radio (WoR) via CAD

- **The Problem:** Constant RX mode draws 15mA, which drains the battery even when no packets are sent.
- **The Solution:** Use RadioLib's `ChannelActivityDetection (CAD)`.
- **Logic:**
  1. Radio wakes every 500ms for a "sniff" (2-4ms).
  2. If preamble is detected, stay awake for the full packet.
  3. If air is clear, return to sleep.
- **Risk:** Missed preambles if transmitter and receiver are out of sync. Requires extended preambles on the transmitter side.

### 2.2 Solar Velocity Analysis

- Store the last 5 battery readings in a ring buffer.
- Calculate `delta_V / delta_T`.
- **Feature:** If the trend is negative and below 3.6V, proactively enter "Hibernate" even if the current voltage is technically in "Normal".

---

## 📋 Approval Requested

- [x] Approve v1.6.0 feature set for flashing.
- [x] Approve WoR (CAD) as the next prioritized high-risk research track.
