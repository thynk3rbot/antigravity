
## [2026-03-23 10:54:37] Firmware V2 Regression Fixes

**Technical Context:**
Identified and fixed critical regressions in v2 port: 1. Missing 5s boot stabilization delay causing brownouts. 2. Missing 1.6V TCXO and LDO configuration for SX1262 (Heltec V3/V4). 3. Incorrect Radio Sync Word (0x12). 4. WebApp dictionary mutation and DOM thrashing issues.

**Actionable Rule:**
- [ ] Added to AGENTS.md / PROCESSES.md
- [ ] Verified in current branch

---

## [2026-03-26 13:15:24] Heltec V4 display and buttons unresponsive after boot

**Technical Context:**
Root cause: oled_manager.cpp had a 30s sleep timeout (SLEEP_TIMEOUT_MS) that fired unconditionally regardless of USB power state, causing the display to go dark on all V3/V4 nodes even when plugged in. Additionally, g_buttonHandled was initialized to 'true', silently consuming the first button press (ISR sets it false, but loop skips it because it starts true). Fix (v0.0.14): (1) Gate sleep timeout with PowerManager::isPowered() — when USB powered, display never sleeps. (2) Also wake display immediately if USB power is detected and display is off. (3) Initialize g_buttonHandled = false so first press is not dropped. Never use an unconditional sleep timeout on a status display — always respect power source.

**Actionable Rule:**
- [ ] Added to AGENTS.md / PROCESSES.md
- [ ] Verified in current branch

---

## [2026-03-26 13:17:48] Heltec V4/V3 display sleeps unconditionally — OLED goes dark even on USB power

**Technical Context:**
oled_manager.cpp SLEEP_TIMEOUT_MS (30s) fires regardless of power source. When plugged in via USB, devices should keep the display active. Additionally, g_buttonHandled was initialized to True, silently consuming the very first button press (ISR sets it False, but update() skips it on first pass).

**Fix (v0.0.14):**
- Gate sleep with: if (!PowerManager::isPowered() && (now - g_lastActivityTime >= SLEEP_TIMEOUT_MS))
- Auto-wake when USB reconnects: if (PowerManager::isPowered() && !g_displayOn) setDisplayOn(true)
- Initialize g_buttonHandled = false (not true)

**Actionable Rule:**
- Never use unconditional display sleep — always respect isPowered()
- Always initialize button ISR handled-flag to false so first press registers

---

## [2026-03-26] Heltec V4/V3 OLED Sleep Unconditional â€” Display Goes Dark on USB Power

**Technical Context:**
oled_manager.cpp SLEEP_TIMEOUT_MS (30s) fires regardless of power source. When plugged in, display should stay on. Also g_buttonHandled=true init consumed first button press silently.

**Fix (v0.0.14):** Gate sleep with PowerManager::isPowered(). Auto-wake when USB detected. Init g_buttonHandled=false.

**Rule:** Never use unconditional display sleep. Always respect isPowered(). Init button ISR flags to false.

---
