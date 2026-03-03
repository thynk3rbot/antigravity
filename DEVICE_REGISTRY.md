# LoRaLink Testing Devices Registry

## Device Identification & Firmware Status

This file tracks the two Heltec ESP32 LoRa V3 devices used for LoRaLink testing and deployment.

---

## Device 1: MASTER

| Property | Value |
|----------|-------|
| **Role** | Master/Primary node |
| **Hardware** | Heltec ESP32 LoRa V3 (ESP32-S3) |
| **MAC Address** | `10:51:db:58:e6:c8` |
| **Serial Port** | COM13 (current) |
| **Firmware Version** | v1.4.9 |
| **Config Profile** | `heltec_v3_farm_automation.json` |
| **mDNS Hostname** | `master.local` |
| **Network Mode** | DHCP + mDNS |
| **Flash Date** | 2026-03-02 |
| **Status** | ✅ Active |

**Notes:**
- Primary device with full farm automation capabilities
- Configured for irrigation pump, grow lights, temperature monitoring
- Supports WiFi, LoRa, BLE, ESP-NOW, MQTT telemetry

---

## Device 2: SLAVE

| Property | Value |
|----------|-------|
| **Role** | Secondary/Test node |
| **Hardware** | Heltec ESP32 LoRa V3 (ESP32-S3) |
| **MAC Address** | `10:51:db:51:fc:c4` |
| **Serial Port** | COM7 (current) |
| **Firmware Version** | v1.4.9 (pending flash) |
| **Config Profile** | `heltec_v3_generic.json` |
| **mDNS Hostname** | `slave.local` |
| **Network Mode** | DHCP + mDNS |
| **Flash Date** | Pending |
| **Status** | ⏳ Flashing in progress |

**Notes:**
- Secondary device for testing and comparison
- Generic GPIO control configuration for bench testing
- Supports WiFi, LoRa, BLE, ESP-NOW

---

## Quick Reference

### By MAC Address
- `10:51:db:58:e6:c8` → **MASTER** (COM13)
- `10:51:db:51:fc:c4` → **SLAVE** (COM7)

### By mDNS Hostname
- `master.local` → Device 1 (MASTER)
- `slave.local` → Device 2 (SLAVE)

### By Serial Port
- **COM13** → MASTER (10:51:db:58:e6:c8)
- **COM7** → SLAVE (10:51:db:51:fc:c4)

---

## Flashing Procedure

When flashing firmware to specific devices:

1. **Flash MASTER** (COM13):
   ```bash
   pio run -t upload -e heltec_wifi_lora_32_V3
   # Auto-detects COM13, MAC 10:51:db:58:e6:c8
   ```

2. **Flash SLAVE** (COM7):
   ```bash
   # Disconnect MASTER from USB
   # Connect SLAVE to USB
   pio run -t upload -e heltec_wifi_lora_32_V3
   # Auto-detects COM7, MAC 10:51:db:51:fc:c4
   ```

---

## Configuration Deployment

| Device | Config File | Pin Configuration | Purpose |
|--------|-------------|-------------------|---------|
| MASTER | `heltec_v3_farm_automation.json` | Irrigation, Lights, Heater, DHT Sensor | Full farm automation |
| SLAVE | `heltec_v3_generic.json` | LED, Relays, Battery Sensor | Generic GPIO testing |

---

## Webapp URLs

- **Webapp Server:** `http://localhost:8000`
- **Master Device API:** `http://master.local/api/status`
- **Slave Device API:** `http://slave.local/api/status`
- **Discovery Scan:** `http://localhost:8000/api/discover`

---

## Last Updated

- **Date:** 2026-03-02
- **By:** Build & Flash Session
- **Firmware Version:** v1.4.9
