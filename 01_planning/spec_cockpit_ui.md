---
status: complete
owner: human
---

# Specification: LoRaLink Cockpit UI Overhaul

## 1. Objective
Consolidate the fragmented dashboard (index, devqa, scheduler, product_builder) into a single, high-density "Cockpit" interface to improve operator situational awareness for mission-critical deployments.

## 2. Requirements

### 2.1 UI Structure
- **Field Mode**: Simplified view for real-time telemetry and command input.
- **Factory Mode**: Administrative view for hardware configuration and settings.
- **Transport Matrix**: Real-time status indicators for HTTP, BLE, Serial, and LoRa links.
- **HUD Stat Cards**: Compact display for Battery, RSSI, Heap, and Loop timing.

### 2.2 Functional Blocks
- **Node Registry Sidebar**: Dynamic list of nearby peers with rescan capability.
- **AI Copilot Panel**: Placeholder for automated diagnostic suggestions.
- **Log Stream**: Integrated console for system traces and command feedback.
- **Command Entry**: Command line interface that speaks to the `CommandManager`.

## 3. Implementation Details
- **Static Assets**: All new UI logic resides in `tools/webapp/static/cockpit.html`.
- **Styling**: Migration from inline styles to `tools/webapp/static/css/cockpit.css`.
- **Legacy Preservation**: Old dashboard files (index, devqa, etc.) are archived to `static/legacy/`.

## 4. Validation Plan
- [x] Verify server serves `cockpit.html` by default.
- [x] Test mode switching (Field vs Factory).
- [x] Verify transport chips correctly reflect manager states.
