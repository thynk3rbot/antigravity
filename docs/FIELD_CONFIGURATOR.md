# Field Configurator — Configure Dashboard Metrics

**What:** A web-based tool to define which device metrics (fields) appear on:
- **STATUS** cards: Overview metrics shown on the main dashboard grid
- **VSTATUS** views: Detailed metrics shown when clicking a device for full details

**Why:** Different device classes (V3/V4) and different operators need different metrics. The configurator lets you customize without recompiling firmware or restarting the daemon.

---

## Quick Start

1. **Open the configurator:**
   ```
   http://localhost:8000/configurator.html
   ```

2. **Edit metrics:**
   - **Left panel (STATUS):** Metrics shown on grid cards (device name, battery %, signal)
   - **Right panel (VSTATUS):** Metrics shown in detail view (full telemetry, GPS, uptime)

3. **Save changes:**
   - Toggle visibility (eye icon on/off)
   - Reorder fields (↑ ↓ buttons)
   - Delete fields (✕ button)
   - Click "Save Changes" button

4. **Apply to devices:**
   - Changes take effect immediately
   - Dashboard refreshes on next poll (10 seconds)
   - New devices see the updated field definitions

---

## Configuration Structure

### Device Classes
- **V3:** Heltec WiFi LoRa 32 V3 (ESP32-S3)
- **V4:** Heltec WiFi LoRa 32 V4 (variant)

Each class has independent STATUS and VSTATUS configurations.

### Field Definition
```json
{
  "key": "bat_pct",           // Firmware field identifier
  "label": "Battery %",        // Human-readable label for dashboard
  "type": "percent",           // Data type (string, percent, dbm, duration, etc.)
  "visible": true,             // Show on dashboard by default
  "order": 3,                  // Display order (lower = earlier)
  "critical": true             // Critical metric (may trigger alerts)
}
```

### Data Types
- `string` — Text (device name, mode, ID)
- `percent` — 0-100% (battery percentage, signal quality)
- `dbm` — Decibel milliwatts (RSSI, signal strength, range -120 to 0)
- `millivolts` — Battery voltage (e.g., 3400 mV)
- `duration` — Time elapsed (uptime, seconds to human-readable)
- `integer` — Whole numbers (peer count, port numbers)
- `float` — Decimal numbers (SNR, temperature)
- `ipv4` — IP address (device address)

---

## Common Workflows

### 1. Show Only Battery and Signal on Dashboard
**V3 STATUS (Overview):**
1. Click V3 tab
2. Left panel (STATUS): Toggle to show only:
   - Device Name
   - Battery %
   - Signal dBm
3. Hide: Firmware, Uptime, Peers, Mode
4. Save Changes

### 2. Add Temperature to Detailed View
**V4 VSTATUS (Detailed):**
1. Click V4 tab
2. Right panel (VSTATUS): Enable GPS Location (if sensor available)
3. Reorder: Move Temperature above GPS
4. Save Changes

### 3. Highlight Critical Metrics
Critical metrics are marked with red badges. These are:
- `bat_pct` — Battery percentage (device won't last long if low)
- `bat_mv` — Battery voltage (critical for power management)
- Any custom field marked `"critical": true`

---

## Persistent Storage

Configuration is stored in:
```
daemon/field_definitions.json
```

This file is:
- **Editable:** Human-readable JSON, can edit directly if needed
- **Persistent:** Survives daemon restarts
- **Versioned:** Includes `last_updated` timestamp
- **Atomic:** File locking prevents concurrent edits

---

## API Endpoints

### Get visible field definitions
```bash
GET /api/config/fields/{device_class}/{status_type}
# Example: GET /api/config/fields/v3/status
```

### Get all fields (including hidden)
```bash
GET /api/config/fields/{device_class}/all
# Example: GET /api/config/fields/v4/all
```

### Update field definitions
```bash
PUT /api/config/fields/{device_class}/{status_type}
# Body: { "fields": [...] }
```

### Get entire configuration
```bash
GET /api/config/all
```

### Reset to defaults
```bash
POST /api/config/reset/{device_class}
# Resets one class. Omit device_class to reset all.
```

### Health check
```bash
GET /api/config/health
```

---

## Troubleshooting

### Changes not appearing on dashboard
1. Save Changes button was clicked?
2. Toast notification showed "Configuration saved successfully"?
3. Wait 10 seconds for next dashboard refresh poll
4. Try F5 to force browser reload

### "Failed to save: ..."
- Check browser console (F12 → Console tab)
- Check daemon logs: `tail -f logs/daemon.log`
- Verify daemon is running: `curl http://localhost:8001/health`

### Field definitions file corrupted
1. Click "Reset to Defaults" in configurator
2. Or manually delete `daemon/field_definitions.json`
3. Restart daemon (will regenerate with defaults)

---

## Advanced: Custom Field Types

To add a new field type (e.g., RGB color, temperature with unit):

1. **Firmware:** Add field to STATUS or VSTATUS command response
2. **Configurator:** Add `"type": "your_type"` to field definition
3. **Dashboard:** Add CSS class for rendering (e.g., `.field-type-rgb`)
4. **Example:**
   ```json
   {
     "key": "led_color",
     "label": "LED Color",
     "type": "rgb",
     "visible": true,
     "order": 15
   }
   ```

---

## Example: Full Custom Configuration

To use custom field definitions, edit `daemon/field_definitions.json` directly:

```json
{
  "version": "1.0",
  "last_updated": "2026-03-31T12:00:00Z",
  "defaults": {
    "v3": {
      "status": {
        "display_name": "V3 Minimal",
        "fields": [
          { "key": "name", "label": "Device", "type": "string", "visible": true, "order": 1 },
          { "key": "bat_pct", "label": "Battery", "type": "percent", "visible": true, "order": 2, "critical": true },
          { "key": "lora_rssi", "label": "Signal", "type": "dbm", "visible": true, "order": 3 }
        ]
      },
      "vstatus": {
        "display_name": "V3 Full",
        "fields": [
          // ... all fields, visible and hidden
        ]
      }
    }
  }
}
```

---

## Dashboard Integration

The dashboard uses the configurator:

1. **On load:** Fetches field definitions via `/api/config/fields/v3/status`
2. **Renders:** Only visible fields, in order
3. **On refresh:** Refetches field definitions (supports live changes)
4. **Device click:** Shows VSTATUS with fields from `/api/config/fields/v3/vstatus`

---

**Status:** ✅ Field Configurator is production-ready. Edit metrics without downtime.
