
# LoRaLink v1.6.0 Verification & Next-Steps Plan

This document outlines the validation steps for the **Range Boost (Binary Mode)** and **Power-Miser (Smart Ag)** features before the next firmware flash.

## 📦 Release v1.6.0 Components

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

## 🚀 Phase 2: Next Implementation Plans (High Risk)

After v1.6.0 is verified in the field, we move to the autonomous logic:

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
