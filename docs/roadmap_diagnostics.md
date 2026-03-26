# Roadmap: Remote Field Diagnostics

**Objective:** Integrate manufacturer-grade diagnostic routines into the production firmware to allow remote triage and health monitoring of LoRaLink nodes in the field.

---

## **Phase 1: Stabilization (Current Focus)**
- [x] Identify and verify V4 GPS Hardware Pinout (GPIO 34 Power, 39/38 UART).
- [x] Establish strict versioning policy (MAJOR.MINOR.POINT-PLATFORM).
- [ ] Confirm `gps_chars > 0` on all V4 fleet devices.
- [ ] Monitor UART stability under varied power conditions (USB vs. Battery).

---

## **Phase 2: Diagnostic Integration (Future)**
### **1. Hardware Truth Manager**
- Implement a `DiagnosticManager` to wrap periodic hardware self-checks.
- **GNSS Triage:** A 30-second "Deep Scan" that bypasses standard filters to log raw NMEA traffic.
- **RF Health:** Short-burst LoRa Ping-Pong (RSSI/SNR mapping) without requiring a mesh link.
- **Power Integrity:** Voltage drop monitoring during high-current LoRa TX bursts.

### **2. Web Dashboard Extensions**
- **Maintenance Tab:** UI for triggering diagnostic routines on-demand.
- **Health JSON:** API endpoint (`/api/system/health`) returning a comprehensive peripheral status object.
- **Error Codes:** Standardized hardware failure codes (e.g., `HW_GPS_01`: UART Timeout).

---

## **Phase 3: Remote "Triage" OS**
- Deploy a minimal "Diagnostic Bootloader" for critical field recovery.
- Node can pull a factory-test binary via OTA to validate hardware, then revert to production firmware.

---

## **Key Hardware Parameters (V4)**
- **GNSS Power:** GPIO 34 (Active LOW)
- **GNSS UART:** RX:39, TX:38 (9600 Baud Factory Default)
- **Reset:** GPIO 42 (Hold HIGH)
- **Wake:** GPIO 40
- **PPS:** GPIO 41
