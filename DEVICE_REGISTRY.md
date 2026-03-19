# LoRaLink Fleet Registry (Deployment Configuration)

This file tracks the active devices in the LoRaLink fleet and their current verified firmware status.

---

## Device 1: Peer1 (Gateway)

| Property | Value |
|----------|-------|
| **Previous Role** | Master |
| **Hardware** | Heltec ESP32 LoRa V3 (ESP32-S3) |
| **MAC Address** | `10:51:db:58:e6:c8` |
| **Serial Port** | [Auto-Select] |
| **IP Address** | `172.16.0.27` |
| **Firmware Version** | **v0.1.0** (Verified 2026-03-12) ✅ |
| **mDNS Hostname** | `peer1.local` |
| **Network Mode** | Static IP |
| **Flash Date** | 2026-03-12 |

**Notes:**
- Acting as the primary entry point for the fleet.
- Persisted in `.settings.json` as `ble_prefix_a`.

---

## Device 2: Peer2 (Node)

| Property | Value |
|----------|-------|
| **Previous Role** | Slave |
| **Hardware** | Heltec ESP32 LoRa V3 (ESP32-S3) |
| **MAC Address** | `10:51:db:51:fc:c4` |
| **Serial Port** | [Auto-Select] |
| **IP Address** | `172.16.0.26` |
| **Firmware Version** | **v0.1.0** (Verified 2026-03-12) ✅ |
| **mDNS Hostname** | `peer2.local` |
| **Network Mode** | Static IP |
| **Flash Date** | 2026-03-12 |

**Notes:**
- Acting as the secondary test node.
- Persisted in `.settings.json` as `ble_prefix_b`.

---

## Fleet Verification Status (Baseline)

| Milestone | Status | Details |
|-----------|--------|---------|
| **Version Sync** | ✅ PASS | Both devices verified on v0.1.0 binary. |
| **OTA Stability** | ✅ PASS | Successfully updated via `ota_master` / `ota_slave` envs. |
| **Registry Mirroring** | ✅ PASS | Webapp correctly maps internal Registry to Peer1/Peer2 labels. |

---

## Deployment Shortcuts

Always use the unified script to prevent version drift:
```powershell
.\tools\deploy_dual.ps1
```

---

## Last Updated
- **Date:** 2026-03-12
- **By:** Antigravity (Harmonization Session)
- **Firmware Baseline:** v0.1.0
