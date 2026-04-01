# 🏭 Factory Commissioning — Virgin Device Setup

**For AG (Authorized Technician) Only**

This guide covers provisioning virgin (never-flashed) Heltec devices with Magic firmware and registering them in the fleet.

---

## Process Overview

1. **Connect** virgin device via USB
2. **Flash** firmware (auto-detects hardware version or allows manual selection)
3. **Register** device in fleet registry with unique ID
4. **Test** device comes online and reports to daemon

**Total time per device: ~2-3 minutes**

---

## Prerequisites

- Windows 10+ or Linux/Mac
- PlatformIO installed: `pip install platformio`
- Magic daemon source code available
- USB drivers for CH340/CP2102 (usually auto-install on Windows)

---

## Single Device Flash (Interactive)

### Quick Start

1. **Connect** virgin Heltec device via USB cable (any port)
2. **Double-click** `Factory_USB_Flasher.bat` from repo root
3. **Select device** if multiple are connected
4. **Confirm hardware version** (V3 or V4) — auto-detect usually works
5. **Review confirmation** and type `yes` to start flash
6. **Wait** for firmware to compile and upload (~30-60 seconds)
7. **Enter device ID** (e.g., `DEV001`, `GATEWAY_01`, etc.) to register in fleet
8. **Done** — device will boot, connect to WiFi, and appear in fleet dashboard

### Example Run

```
[Init] Detecting USB devices...
[Found] COM3: Heltec WiFi LoRa 32 (ESP32-S3) - USB UART (v4)

[Auto-select] Using single device: COM3
[Auto-detect] Hardware version: v4

⚠️  About to flash firmware to: COM3 (v4)
Continue? (yes/no): yes

[Flash] Starting firmware upload to COM3 (heltec_v4)...
  Platform Manager: Installing espressif32 @ ~6.0.0
  ...
  [Verify] Device appears to be online (firmware flashed)

Enter device ID (or press Enter to skip registry): DEV001
[Registry] Device DEV001 registered as commissioned

✓ Flash complete!
```

---

## Batch Flash (Multiple Devices)

Use batch mode to flash multiple devices sequentially without interaction.

### Setup

1. **Create CSV file** with device list:
   ```
   COM3,v4,DEV001
   COM4,v4,DEV002
   COM5,v3,DEV003
   ```
   Format: `port,hardware_version,device_id`

2. **Run batch flasher:**
   ```powershell
   python tools/usb_flasher.py --batch your_devices.csv
   ```

3. **Monitor progress** — flasher reports success/failure per device

### Batch CSV Format

```csv
# USB Flasher Batch — Factory Commissioning
# Format: port,hardware_version,device_id
# Lines starting with # are comments; blank lines ignored

COM3,v4,DEV001
COM4,v4,DEV002
COM5,v3,DEV003
```

### Example Batch Run

```
[Init] Detecting USB devices...
[Found] COM3: Heltec WiFi LoRa 32 (ESP32-S3)
[Found] COM4: Heltec WiFi LoRa 32 (ESP32-S3)
[Found] COM5: Heltec WiFi LoRa 32 V3

[Batch 1] Port=COM3, HW=v4, ID=DEV001
  [Flash] Starting firmware upload to COM3...
  [Registry] Device DEV001 registered as commissioned
  ✓

[Batch 2] Port=COM4, HW=v4, ID=DEV002
  [Flash] Starting firmware upload to COM4...
  [Registry] Device DEV002 registered as commissioned
  ✓

[Batch 3] Port=COM5, HW=v3, ID=DEV003
  [Flash] Starting firmware upload to COM5...
  [Registry] Device DEV003 registered as commissioned
  ✓

[Summary] Successfully flashed 3/3 devices
```

---

## Advanced Options

### List Detected Devices Only

```bash
python tools/usb_flasher.py --list
```

### Skip Verification (Faster)

```bash
python tools/usb_flasher.py --no-verify
```

### Skip Registry Registration

(Device still flashes, but not registered in fleet)

```bash
python tools/usb_flasher.py --no-register
```

### Specific Port + Hardware

```bash
python tools/usb_flasher.py --port COM3 --hw v4
```

### Manual Registry Entry

If you skip registration but want to add device later:

```python
from daemon.src.device_registry import DeviceRegistry

registry = DeviceRegistry("daemon/data/device_registry.db")
registry.insert_or_update(
    device_id="DEV001",
    hardware_class="V4",
    ip_address="",  # Will be auto-detected on first boot
    firmware_version="0.0.154V4",
    status="commissioned"
)
```

---

## Troubleshooting

### "No USB devices detected"

- **Check connections:** Device may not be fully seated in USB port
- **Check drivers:** Windows Device Manager → `Ports (COM & LPT)` should show USB UART
  - Missing? Download [CP2102 drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- **Try different port:** Some USB hubs have power/communication issues
- **Restart device:** Unplug 10 seconds, reconnect

### "PlatformIO not found"

Install globally:
```bash
pip install platformio
pio system info
```

### "Port is in use" / "Upload failed"

- Check if another Serial Monitor is open (IDE, PuTTY, etc.) — close it
- Unplug device, wait 5 seconds, plug back in

### "Hardware version auto-detect failed"

Device still shows in list but version is unknown. You'll be prompted — just type `v3` or `v4` manually.

### Device Flashed But Not Appearing in Fleet

- **Wait 30-60 seconds** — device needs to boot and connect to WiFi
- **Check WiFi config:** Does it know your SSID/password?
  - If not: Use `SETWIFI` command over serial to set credentials
- **Check device IP:** Open router admin panel, look for recent DHCP leases
  - If found: Device is online, may need manual IP registration
  - If not found: Device can't connect to WiFi — check credentials

---

## After Commissioning

### Verify Device is Online

1. **Open Fleet Dashboard:** http://localhost:8000
2. **Check device list** — new device should appear within 1 minute
3. **Check status:** Click device → view battery, uptime, last seen

### Update Device Config (Optional)

Use daemon API to set device name, IP preferences, etc.:

```bash
curl -X POST http://localhost:8001/api/registry/devices/DEV001 \
  -H "Content-Type: application/json" \
  -d '{"device_id": "DEV001", "name": "LoRa Gateway - Front Yard"}'
```

### Firmware Updates

Once commissioned, devices can be updated via OTA:
- **From daemon:** Device > OTA Flash
- **Bulk:** Swarm OTA panel, select multiple devices, flash all at once

---

## Hardware Classes

| Hardware | Alias | Processor | Radio | PlatformIO Env |
| --- | --- | --- | --- | --- |
| Heltec WiFi LoRa 32 V3 | V3 | ESP32-S3 | SX1262 | `heltec_v3` |
| Heltec WiFi LoRa 32 V4 | V4 | ESP32-S3 (variant) | SX1262 | `heltec_v4` |

**Critical:** Do NOT flash V3 firmware to a V4 device or vice versa — hardware class mismatch will cause brown-out resets and failures.

---

## Device ID Naming Convention

Suggested naming for fleet organization:

- **Gateways:** `GW_01`, `GW_02`, etc.
- **Sensors:** `TEMP_01`, `MOIST_02`, `WIND_03`, etc.
- **Simple sequential:** `DEV_001`, `DEV_002`, `DEV_003`, etc.
- **Deployed location:** `FIELD_NORTH_01`, `BUILDING_SOUTH_02`, etc.

**Keep IDs ≤32 characters** (firmware limit). Avoid special characters; use `_` or `-` as separators.

---

## Commissioning Checklist

- [ ] Virgin device connected via USB
- [ ] Correct hardware version identified (V3 or V4)
- [ ] Firmware flashed successfully (no errors)
- [ ] Device registered in fleet registry with unique ID
- [ ] Device appears in Fleet Dashboard within 1 minute
- [ ] Device status shows "online" and recent "last seen"
- [ ] Battery voltage and RSSI are reasonable values
- [ ] Device ID is meaningful and documented

---

## Support

If flashing fails:
1. **Check USB connection** — is it a high-power hub or direct PC port?
2. **Check device drivers** — see Troubleshooting section
3. **Check PlatformIO** — run `pio system info` to verify install
4. **Check firmware path** — ensure `firmware/magic/` exists and is readable
5. **Report error** with exact message + device details to team

---

**Last updated:** 2026-03-31 | **Version:** 1.0 | **Status:** Factory Ready ✓
