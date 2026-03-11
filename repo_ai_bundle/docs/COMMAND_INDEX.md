# COMMAND_INDEX.md

Canonical index of commands for LoRaLink firmware.

This file is intended to be the shared command contract for:
- firmware
- PC-side tools
- web dashboard
- AI coding agents

## Status

This file is a **governance and maintenance scaffold**.
It should be treated as authoritative once the command table is fully populated from the current repo.

Because command registration appears to be centered in `src/managers/CommandManager.cpp`, this file must be updated whenever the command surface changes.

## Maintenance Rule

Whenever a command is:
- added
- removed
- renamed
- moved to a different manager
- exposed to a new interface
- reclassified by safety level
- reclassified by domain

this file **must** be updated in the same commit.

If command behavior changes, also inspect and update the coupled tool files as needed:
- `tools/ble_instrument.py`
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`

## Recommended Validation Rule

Before merging command-related changes:
1. verify the firmware build passes
2. update this file
3. update any coupled tools
4. verify command exposure and safety expectations
5. optionally run a repo check script that compares documented commands against the command registry

## Command Domains

| Domain | Meaning |
|---|---|
| system | status, identity, version, help, basic lifecycle |
| network | WiFi, BLE, LoRa, MQTT, ESP-NOW, transport config |
| hardware | GPIO, relay, PWM, servo, reads, direct I/O |
| scheduler | scheduled tasks, timers, dynamic task configuration |
| product | product deployment, preset behavior, atomic configuration bundles |
| diagnostic | tracing, radio diagnostics, development-only inspection |
| messaging | text messaging, routing, payload forwarding |

## Safety Levels

| Safety | Meaning |
|---|---|
| safe | read-only or low-risk |
| admin | configuration or state mutation |
| hardware | affects physical outputs or device-side actuation |
| restricted | potentially destructive or sensitive |
| dev-only | intended only for development or diagnostic builds/modes |

## Interfaces Vocabulary

Use these labels consistently:
- `SERIAL`
- `BLE`
- `WIFI`
- `LORA`
- `ESPNOW`
- `MQTT`
- `ALL`
- `LOCAL_ONLY`

## Command Table

> Replace `TBD` values only after confirming the implementation in code.
> Do not guess.

| Command | Domain | Primary Manager | Allowed Interfaces | Tool Coupling | Safety | Notes |
|---|---|---|---|---|---|---|
| HELP | system | CommandManager | ALL | ble_instrument | safe | Should list or summarize available commands. |
| STATUS | system | CommandManager | ALL | webapp, ble_instrument | safe | Device health / summary. |
| SETNAME | system | CommandManager | ALL | webapp | admin | Device identity mutation. |
| SETWIFI | network | WiFiManager / CommandManager | BLE, WIFI, SERIAL | webapp | admin | Should likely be restricted by mode. |
| TRANS | network | CommandManager | ALL | ble_instrument | safe | Transport info or transport selection. |
| PING | system | CommandManager | ALL | ble_instrument | safe | Connectivity test and routing validation. |
| BEACON | network | LoRaManager / CommandManager | LORA, ALL | none | safe | Radio or discovery behavior. |
| GPIO | hardware | CommandManager | ALL | webapp, ble_instrument | hardware | Must respect pin conflict rules. |
| READ | hardware | CommandManager | ALL | ble_instrument | safe | Read pin or input state. |
| PWM | hardware | CommandManager | ALL | webapp | hardware | Output modulation. |
| SERVO | hardware | CommandManager | ALL | webapp | hardware | Physical actuation. |
| SCHED | scheduler | ScheduleManager / CommandManager | ALL | webapp, ble_instrument | admin | Dynamic schedule/task control. |
| PRODUCT | product | ProductManager / CommandManager | ALL | webapp | admin | Product deployment/config bundle behavior. |
| RADIO | diagnostic | LoRaManager / CommandManager | ALL | none | dev-only | Verify duplicate registration risk in code. |

## Command Review Checklist

When any command changes, verify:
- domain classification is still correct
- safety level is still correct
- allowed interfaces are still correct
- tool coupling is still correct
- mode or device-class gating is needed or updated
- any user-facing docs need refresh

## Future Enhancements

Recommended next step:
- add a repo script such as `tools/check_command_index.py`
- extract command registrations from the firmware
- compare against this file
- fail CI or pre-commit when they drift

## Notes for AI Agents

When editing command behavior:
1. inspect `src/managers/CommandManager.cpp`
2. inspect any manager delegated to by the command
3. inspect coupled files in `tools/`
4. update this file in the same change
5. document uncertainty rather than guessing
