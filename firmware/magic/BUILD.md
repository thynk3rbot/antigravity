# Magic v2 Build Guide

## Prerequisites

### 1. Install PlatformIO
PlatformIO is required to build Magic v2 for ESP32 boards.

#### Option A: Install via pip (Recommended)
```bash
pip install platformio
```

#### Option B: Install via Homebrew (macOS)
```bash
brew install platformio
```

#### Option C: Download IDE
Visit https://platformio.org/platformio-ide and download for your OS.

### 2. Verify Installation
```bash
platformio --version
```

You should see version 6.0+ displayed.

### 3. Install ESP32 Platform (One-Time)
```bash
platformio platform install espressif32
```

This downloads the toolchain and libraries (~2GB).

---

## Build Commands

### Build for Hub (Heltec V2 - SX1276)
```bash
cd magic-v2
platformio run --environment heltec_v2_hub
```

**Output:** `.pio/build/heltec_v2_hub/firmware.elf` (binary)

### Build for Node (Heltec V3 - SX1262, USB Serial)
```bash
platformio run --environment heltec_v3_node
```

**Output:** `.pio/build/heltec_v3_node/firmware.elf`

### Build for Node (Heltec V4 - SX1262, Native USB + PSRAM)
```bash
platformio run --environment heltec_v4_node
```

**Output:** `.pio/build/heltec_v4_node/firmware.elf`

### Build All Environments
```bash
platformio run
```

---

## Upload to Device

### Prerequisites
- Device connected via USB
- Check COM port: `platformio device list`

### Upload Hub (V2)
```bash
platformio run --environment heltec_v2_hub --target upload
```

### Upload Node (V3)
```bash
platformio run --environment heltec_v3_node --target upload
```

### Upload Node (V4)
```bash
platformio run --environment heltec_v4_node --target upload
```

---

## Serial Monitor

### Watch Live Output
```bash
platformio device monitor --baud 115200
```

Ctrl+C to exit.

### Monitor + Build (Useful for development)
```bash
platformio run --environment heltec_v2_hub && platformio device monitor --baud 115200
```

---

## Troubleshooting

### Build Fails: "board not found"
```
Error: Unknown board ID 'heltec_wifi_lora_32'
```

**Solution:** Install platform first
```bash
platformio platform install espressif32
```

### Build Fails: "Radio library not found"
```
Error: No macro info available for library 'RadioLib'
```

**Solution:** PlatformIO will auto-download on first build. Wait for library resolution, then retry:
```bash
platformio run --environment heltec_v2_hub
```

### Upload Fails: "Port not found"
```
Error: Could not find serial port
```

**Solution:**
1. Check connection: `platformio device list`
2. On Windows, look for "COM#" port
3. Add to platformio.ini if needed:
   ```ini
   upload_port = COM3
   ```

### Device Boots but No Serial Output
- Check baud rate is 115200
- On V4 (Native USB), device appears as "COM#" without CH340 driver
- Try reboot: Press EN (Reset) button on device

---

## Build Configuration

### Compile Flags (from platformio.ini)
Each environment defines flags that control features:

```ini
[env:heltec_v2_hub]
build_flags =
    -D ROLE_HUB              # This is the hub
    -D RADIO_SX1276          # Uses SX1276 radio
    -D ARDUINO_HELTEC_WIFI_LORA_32

[env:heltec_v3_node]
build_flags =
    -D ROLE_NODE             # This is a node
    -D RADIO_SX1262          # Uses SX1262 radio
    -D ARDUINO_HELTEC_WIFI_LORA_32_V3

[env:heltec_v4_node]
build_flags =
    -D ROLE_NODE
    -D RADIO_SX1262
    -D ARDUINO_USB_MODE=1           # Native USB
    -D ARDUINO_USB_CDC_ON_BOOT=1    # Serial over USB
    -D BOARD_HAS_PSRAM              # 2MB PSRAM available
    -D ARDUINO_HELTEC_WIFI_LORA_32_V4
```

### Custom Build Flags
Edit `platformio.ini` to add custom flags:

```ini
build_flags =
    ${common_build_flags}
    -D DEBUG_ENABLED
    -D TELEMETRY_INTERVAL=5000
```

---

## Development Workflow

### Fast Edit-Build-Test Loop
```bash
# Terminal 1: Watch for changes and build
platformio run --environment heltec_v2_hub -w

# Terminal 2: Monitor output
platformio device monitor --baud 115200
```

### Clean Build (if strange errors)
```bash
platformio run --environment heltec_v2_hub --target clean
platformio run --environment heltec_v2_hub
```

### Check Firmware Size
```bash
platformio run --environment heltec_v2_hub --target size
```

Output shows:
```
Program:   250,000 bytes
Data:       45,000 bytes
(limits vary by board)
```

---

## Expected Output on Serial Console

Boot sequence (115200 baud):

```
=== Magic v2 Boot ===
Version: 0.3.0
Role: HUB
Radio: SX1276 (V2)
PSRAM: No

[1/6] Initializing HAL...
  ✓ HAL initialized
[2/6] Initializing transports...
  [LoRaTransport] Initialized
  ✓ Transport initialized
[3/6] Initializing application...
  ✓ Hub mode
  [MeshCoordinator] Initialized
  ✓ Mesh coordinator initialized
[4/6] Creating FreeRTOS tasks...
  ✓ Tasks created
[5/6] Boot complete!
  Uptime: 1234 ms

[6/6] Entering main loop (FreeRTOS)...
===========================

[RadioHAL] SX1276 initialized
[STATUS] Uptime: 10 s, Neighbors: 0, Relayed: 0
```

---

## Library Dependencies

All dependencies are auto-downloaded by PlatformIO:

- **RadioLib** 6.4.0 — LoRa radio driver
- **Adafruit SSD1306** 2.5.0 — OLED display driver
- **Adafruit GFX** 1.11.0 — Graphics library
- **PubSubClient** 2.8 — MQTT client (optional)
- **Arduino ESP32 Core** 3.0+ — Built-in

No manual installation needed.

---

## Files Generated After Build

```
magic-v2/
├── .pio/                          # PlatformIO work directory
│   ├── build/
│   │   ├── heltec_v2_hub/
│   │   │   └── firmware.elf       # Binary (V2 Hub)
│   │   ├── heltec_v3_node/
│   │   │   └── firmware.elf       # Binary (V3 Node)
│   │   └── heltec_v4_node/
│   │       └── firmware.elf       # Binary (V4 Node)
│   └── libdeps/                   # Downloaded libraries
├── .platformioenvs/               # Virtual environment (if used)
└── platformio.ini                 # Build configuration
```

---

## Next Steps After Build

1. ✅ **Build succeeds** → Proceed to upload
2. ✅ **Upload succeeds** → Check serial monitor for boot output
3. ✅ **Boot completes** → Devices are ready for mesh communication

---

## Support

- **PlatformIO Docs:** https://docs.platformio.org
- **Heltec Wiki:** https://heltec.org/project/wifi-lora-32-v3/
- **RadioLib Docs:** https://jgromes.github.io/RadioLib/

