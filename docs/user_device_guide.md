# User Guide: Adding Device Support (JSON)

This guide explains how to add support for new hardware to LoRaLink without recompiling the firmware. This is done by creating a "Product Manifest" JSON file and uploading it to the device.

## 1. The Product Manifest (`/products/name.json`)

Each device can have an "Active Product" defined by a JSON file in the `/products` directory of its filesystem.

### Basic Structure
```json
{
  "name": "My Custom Device",
  "pins": [
    { "pin": 32, "mode": "output", "default": 0 },
    { "pin": 14, "mode": "input_pullup" }
  ],
  "plugins": [
    {
      "type": "NutriCalc",
      "config": {
        "pump1": 33,
        "pump2": 25,
        "V3": { "pump1": 34, "pump2": 21 }
      }
    }
  ],
  "schedules": [
    { "id": "fan_toggle", "type": "TOGGLE", "pin": 32, "interval": 3600, "duration": 300 }
  ]
}
```

### Key Sections
- **`pins`**: Standard GPIO configuration. Prevents usage of protected system pins (LoRa, OLED, etc.).
- **`plugins`**: Activates and configures internal modules.
    - `type`: The name of the built-in plugin (e.g., `NutriCalc`).
    - `config`: Parameters for the plugin. Can include board-specific overrides (`V2`, `V3`, `V4`).
- **`schedules`**: Automates GPIO pulses or toggles at specific intervals.

## 2. Deploying to a Device

1. **Upload**: Use the Web App or the `tools/upload_product.py` script to send the JSON file to the device.
2. **Activate**: Use the Serial CLI or MQTT to load the product:
   - CLI: `LOAD my_custom_device`
   - MQTT: Publish `{"command": "LOAD", "args": "my_custom_device"}` to the device topic.
3. **Verify**: The device will reboot or reconfigure its GPIOs immediately. Check the Serial logs for `[ProductManager] Configuring plugin`.

## 3. Board-Specific Pins
If your manifest needs to work across different Heltec versions (V2 vs V3/V4), use the board-specific block in the plugin config:
```json
"config": {
  "V2": { "pin": 33 },
  "V3": { "pin": 17 },
  "V4": { "pin": 42 }
}
```
The firmware will automatically select the correct pin based on the hardware it is running on.
