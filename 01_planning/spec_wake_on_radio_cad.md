# Phase 1: Planning - [P0] [FW] Wake-on-Radio (CAD) Implementation

## 1. Problem Statement
Current "Power-Miser" nodes (slaves) are not practical for long-term battery operation because they must keep the LoRa radio in continuous RX to receive commands. This consumes ~12mA-15mA, draining a battery in days. 

To achieve months/years of battery life, nodes must use **Channel Activity Detection (CAD)** to wake up only when a signal is present.

## 2. Technical Goal
Implement a CAD-based receive cycle in `LoRaManager` that:
1. Reduces average RX current to < 1mA.
2. Automatically wakes to process incoming commands/mesh traffic.
3. Maintains "LOCKSTEP" compatibility: Master nodes must intelligently extend preambles when talking to low-power nodes.

## 3. Architecture & Implementation Plan

### 3.1 Receiver Side (Slave/Power-Miser)
- Use RadioLib's `startReceiveDutyCycleAuto` or an explicit `scanChannel()` loop in `periodicTick()`.
- **Config**: 
  - `CAD_INTERVAL`: 1000ms (Adjustable).
  - `CAD_PREAMBLE_THRESHOLD`: 8 symbols (standard).
- **Behavior**:
  - Radio stays in Deep Sleep.
  - Timer wakes MCU -> MCU wakes Radio -> Radio performs CAD.
  - If Preamble detected: MCU stays awake, Radio enters `RX_SINGLE`.
  - If Timeout/No CAD: Radio goes to Deep Sleep, MCU goes to Light Sleep.

### 3.2 Transmitter Side (Master/Gateway)
- When `IS_MASTER` is true (or when sending to a low-power node):
  - Increase preamble length to `PREAMBLE_LONG` (e.g., 300 symbols) to ensure it spans the recipient's sleep window.
- Use `radio->setPreambleLength(300)` before TX, then reset to 8 after.

### 3.3 Dependencies & Constants
- `RadioLib` supports CAD on SX126x via `scanChannel()`.
- Add to `config.h`:
  - `LORA_CAD_INTERVAL_MS`
  - `LORA_PREAMBLE_LONG`
  - `LORA_PREAMBLE_SHORT` (8)

## 4. Risks & Mitigations
- **Latency**: Commands to low-power nodes will have a delay of up to `CAD_INTERVAL`.
  - *Mitigation*: Acceptable for P0; document in Webapp.
- **Interference**: False CAD triggers (noise) could drain battery.
  - *Mitigation*: Tune CAD parameters (threshold/gain).
- **Master/Slave Drift**: If preambles aren't long enough, nodes will miss packets.
  - *Mitigation*: Rule: `Preamble Length > CAD Interval`.

## 5. Success Criteria
- [ ] Slave node current consumption averages < 2mA (measured/simulated).
- [ ] Master can successfully send a "PING" to a "SLEEPING" slave and get a response.
- [ ] No regression in standard "MAINS_POWERED" node performance.
