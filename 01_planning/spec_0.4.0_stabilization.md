---
status: planning
owner: antigravity
---

# Spec: Firmware 0.4.0 Fleet Stabilization

## 1. Goal
Ensure the entire Heltec fleet (V2, V3, V4) is stabilized on version 0.4.0 with board-specific hardware features and a phased startup sequence.

## 2. Requirements

### A. Phased Protocol Initialization (V1 Parity)
-   **Delay 500ms**: After `PowerManager::init()` / `enableVEXT()` for rail stabilization.
-   **Delay 1000ms**: After `loraTransport.init()` for radio settling.
-   **Delay 2000ms**: After `BLETransport` and before WiFi for protocol stabilization.
-   **OLED Progress**: Call `drawBootProgress` with descriptive labels ("LORA COMM", "BLE MESH") at each step.

### B. Conditional GPS (Hardware-Specific)
-   **Build Flag**: Define `HAS_GPS` in `platformio.ini` EXCLUSIVELY for `heltec_v4`.
-   **Source Isolation**: Wrap `GPSManager`, OLED GPS pages, and `StatusBuilder` reporting in `#ifdef HAS_GPS`.

### C. Flash Stability (V2 Handshake)
-   **Baud Rate**: Set `upload_speed = 115200` for `heltec_v2` to prevent handshake timeouts.
-   **Port Lock Mgmt**: All deployment scripts must terminate `server.py` and `pio` before attempting serial handshake.

## 3. Implementation (Phase 2 Candidate)
Implementations should be performing in `firmware/v2/src/main.cpp`, `lib/App/oled_manager.cpp`, and `platformio.ini`.

## 4. Verification (Phase 3 Audit)
-   Verify build status for `heltec_v2`, `heltec_v3`, `heltec_v4`.
-   Perform visual check of boot progress flow.
-   Validate that non-V4 units do not display GPS data.
