---
status: planning
owner: antigravity
---

# Specification: Mission-Critical Auto-Discovery [P0] [WEB] [LOCKSTEP]

## 1. Objective
Replace the brute-force, hardcoded subnet scanner with a robust, mDNS-based discovery engine that accurately handles Heltec power-cycling and multi-interface (WiFi/BLE/Serial) environments.

## 2. Problem Analysis
- **Subnet Restriction**: Current scanner is hardcoded to `172.16.0.0/24`.
- **Brute Force Inefficiency**: Ping-sweeping 254 IPs is noisy and fails across network boundaries.
- **Power Sensitivity**: Devices in Power-Miser mode may drop mDNS or HTTP packets; discovery must be stateful (remembering last-seen devices).
- **Missing Integration**: Webapp does not currently use standard ZeroConf/mDNS protocols despite Firmware advertising them.

## 3. Requirements

### 3.1 Webapp Backend (`server.py`) [WEB]
- **mDNS Service Discovery**: Integrate the `zeroconf` Python library to listen for `_http._tcp.local` services with the "LoRaLink" prefix.
- **Dynamic Subnet Detection**: If brute-force scan is used as a fallback, it must detect the host's actual local subnet.
- **Stateful Registry**: Transition from a transient "discovered_devices" list to a persistent `KnownDevice` database that tracks `online` status.
- **LOCKSTEP Instrumentation**: Add `LOG_PRINTF` to Firmware when an mDNS query is received (if possible) and log discovery latency in the Webapp.

### 3.2 Firmware Updates (`WiFiManager.cpp`) [FW]
- **mDNS Re-Announcement**: Ensure mDNS is re-initialized if WiFi reconnects after a Power-Miser sleep cycle.
- **Service Tags**: Add custom TXT records to mDNS (e.g., `id=GW-1`, `type=gateway`) to allow the Webapp to identify device types without an HTTP probe.

## 4. Implementation Plan
1.  **Dependency**: Add `zeroconf` to the Python environment (or handle missing gracefully).
2.  **MdnsBrowser Class**: Implement a singleton in `server.py` that maintains a live map of seen `.local` devices.
3.  **Unified API**: Merge mDNS, Serial, and BLE discovery results into a single `/api/nodes` response.
4.  **Firmware Polish**: Update `WiFiManager::init()` to include descriptive TXT records.

## 5. Validation Plan
1.  **mDNS Verify**: Run the Webapp and confirm a device at `LoRaLink-XXXX.local` appears within 2 seconds of joining WiFi.
2.  **Power Test**: Simulate a Heltec power buckle, verify the Webapp marks the node as `OFFLINE` (icon goes grey) but keeps it in the list for easy reconnection.
3.  **Multi-Interface Test**: Confirm nodes found via Serial and WiFi are deduplicated in the Cockpit.
