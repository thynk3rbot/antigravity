---
status: complete
owner: antigravity
---

# Antigravity Audit Report: v1 Firmware Stability & AI Updates
**Date:** 2026-03-18
**Target Firmware:** v1 (v0.2.6)

## To: Claude (Phase 2 Execution)
**Subject:** Porting V1 Critical Fixes to V2 Architecture

Claude, as you build out the V2 FreeRTOS architecture, please ensure you port the following critical fixes and features that have just been merged into the V1 stable branch:

### 1. PowerMiser Re-enabled (CRITICAL STABILITY)
*   **The Problem:** Battery-powered nodes running all four radios (LoRa, ESP-NOW, WiFi, BLE) at 100% duty cycle were suffering massive 500mA brownout spikes, dropping BLE connections. The `PowerManager` policy engine had a temporary `STABILITY_LOCK` forcing it completely off.
*   **The Fix:** We removed the lock. `PowerManager.cpp` now actively steps down to `CONSERVE` and `CRITICAL` mode based on voltage/TTE slope. 
*   **Mesh Publishing:** Transitioning power modes now fires a `"ALL MSG ..."` event via `CommandManager::handleCommand(msg, CommInterface::COMM_INTERNAL)` that alerts the entire mesh.
*   **Discovery Preserved:** Instead of violently shutting off the radios during `CRITICAL` mode (which breaks discovery), the BLE manager now drops advertising down to a slow 160-320ms interval via `boostAdvertising(false)`.
*   **V2 Requirement:** Ensure FreeRTOS idle tasks or power management hooks properly port this baseband throttling logic so discovery never breaks but CPU cycles are saved.

### 2. Local AI Configuration
*   **The Feature:** We added `aiProvider` and `aiModel` support to `DataManager` and `CommandManager` to allow firmware to self-describe its preferred AI endpoint (e.g. `ollama` / `qwen2.5-coder:14b`).
*   **V2 Requirement:** Ensure the NVS keys and `CONFIG SET AI_PROVIDER` endpoints make it into the new V2 CommandManager.

### 3. Immediate Roadmap: LMX Messenger
Once you finalize the V2 testing suites, our next target is building the **LMX Messenger PoC** (bridging the mesh directly to the local Ollama LLM interactions).

Please acknowledge these requirements during your next sprint.
