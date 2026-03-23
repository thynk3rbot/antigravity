# LoRaLink Fleet Deployment Processes

This document serves as the formal record for industrial deployment decisions and stabilized fleet workflows for the LoRaLink project.

## 1. UI & Visual Standards (Industrial V1 Restoration)

To ensure a premium, technically rigorous user experience across all hardware generations (V2, V3, V4), the following visual patterns are enforced:

- **Restored Splash Screen**: All devices must maintain the "V1 Industrial" boot sequence, featuring a framed splash with "SYSTEM BOOT" and the core "LoRaLink" branding.
- **Dynamic Boot Progress**: A real-time progress bar (e.g., `[Init Mesh.. 45%]`) is required throughout the `setup()` sequence.
- **ID Methodology**: Nodes are identified by the **MAC-Suffix/Hardware ID** (e.g., `LL-A4B8D1`). Re-naming is strictly for the node-registry file in the WebApp; the firmware identity is bound to the hardware signature to prevent fleet-wide ID collisions.
- **Branding**: "Any2Any" branding is suppressed in the current stable build to prioritize the primary LoRaLink identity.

## 2. Fleet-Wide Operations

### A. Centralized Flashing (Webapp-Driven)
The LoRaLink Fleet Admin webapp now includes a parallel deployment engine:
- **Parallel Builds**: PlatformIO is triggered by the backend to build and push binary environments (`heltec_v2`, `heltec_v3`, `heltec_v4`) simultaneously.
- **Staggered Uploads**: Serially-attached and OTA devices are addressed in a managed batch to prevent OS port saturation or networking spikes.

### B. Fleet & Targeted Factory Reset
A "clean slate" mechanism is now part of the core firmware:
- **`FACTORY_RESET` Command**: Wipes all NVS settings (WiFi, Node Name, MQTT) and purges all scheduled tasks.
- **Broadcast Reset**: Deploying the command over the Mesh or MQTT triggers parallel resets across the entire fleet for site decommissioning.
- **Targeted Reset**: Using the `FORWARD <id> FACTORY_RESET` mesh command allows resetting a single distant/remote node without physical access.

## 3. Transport Prioritization

- **ESP-NOW**: Enforced as a mandatory "Zero-Config" transport across all units. It must be registered with the `MessageRouter` alongside LoRa and BLE.
- **Transport Status Visibility**: The OLED "Transports" page must report a real-time "ACTIVE/OFF" status for BLE, MQTT, LoRa, and ESP-NOW.

## 4. Stability Rules

- **Initialization Sequence**: `PowerManager` and `VEXT` must stabilize before `Wire.begin()` and `OLEDManager::init()`. This prevents I2C panics during the boot progress calls.
- **Version Lock**: All devices in a fleet deployment zone SHOULD run the same major/minor firmware version to ensure packet structure compatibility.
