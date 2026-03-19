---
status: planning
owner: antigravity
---

# Specification: WiFi Persistence Reliability Fix [P0] [FW] [LOCKSTEP]

## 1. Objective
Fix the intermittent failure where WiFi credentials (SSID/Password) are not persisted across reboots or fail to save correctly in the ESP32 NVS (Non-Volatile Storage).

## 2. Problem Analysis
Based on preliminary review of `DataManager.cpp`:
- **Default Logic Bug**: `p.getString("wifi_ssid", "0.0.0.0")` returns a 7-character string. The subsequent check `if (savedSsid.length() == 0)` will never be true on a first boot, preventing default initialization.
- **Race Conditions**: Multiple Managers (LoRa, WiFi, Data) may be attempting to access NVS during the boot sequence simultaneously.
- **String Handling**: Using `String` objects with `Preferences` can sometimes lead to fragmentation or incomplete writes if the partition is near capacity.

## 3. Requirements

### 3.1 Firmware Fix [FW]
- **Initialization Overhaul**: Correct the `DataManager::Init()` logic to properly detect empty NVS keys.
- **Atomic Writes**: Implement a "Verify after Write" pattern in `SetWifi()`. After calling `p.putString()`, read it back immediately to confirm the write succeeded.
- **Boot Sequence Safety**: Ensure `DataManager::Init()` completes fully before `WiFiManager::init()` attempts to read credentials.
- **LOCKSTEP Instrumentation**: Add `LOG_PRINTF` telemetry for every NVS write and read operation to allow for performance analysis.

### 3.2 Performance Management [LOCKSTEP]
- **Heap Monitoring**: Monitor heap usage before and after NVS operations to ensure `Preferences` is not causing fragmentation.
- **Timing Analysis**: Measure the time spent in `LoadSettings()` and report it via Serial during boot.

## 4. Implementation Plan
1. Modify `DataManager::Init()` to use fixed default checks.
2. Update `DataManager::SetWifi()` to include verification logic.
3. Update `WiFiManager::handle()` to log specific connection failures related to empty/corrupted strings.

## 5. Validation Plan
1. **Manual Test**: Change WiFi credentials via `SETWIFI` command, reboot, and verify they persist.
2. **Stress Test**: Perform 10 consecutive `SETWIFI` operations and verify NVS integrity.
3. **Audit**: Generate Phase 3 Audit report showing memory impact and write success rates.
