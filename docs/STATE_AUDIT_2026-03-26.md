# COMPLETE STATE AUDIT — 2026-03-26

**Status:** 🔴 CRITICALLY FRAGMENTED — Multiple branches, unversioned firmware, uncommitted changes, conflicting objectives

---

## 1. BRANCH FRAGMENTATION

```
main (source of truth)
├── feature/hybrid-proxy-webapp-integration  ← CURRENT (MY WORK: Orion + specs)
├── feature/phase1-provisioning              ← ABANDONED?
├── feature/v2-rationalization               ← AG'S WORK (firmware refactor)
├── feature/v4-gps-mesh-fix                  ← ABANDONED?
└── v2-stability-fix                         ← LOCAL ONLY
```

**PROBLEM:** 5 active feature branches. Nothing merged to `main` since Feb. User said "work from main" but work is scattered across branches.

---

## 2. FIRMWARE VERSIONS — CRITICAL MISMATCH

### Version File (`.version`)
```
0.0.00-2   ← V2 HAS NO ACTUAL VERSION
0.0.00-3   ← V3 HAS NO ACTUAL VERSION
0.2.82-4   ← ONLY V4 IS VERSIONED
```

### V2 platformio.ini
```
Line 30: -D FIRMWARE_VERSION=\"0.0.14\"  ← HARDCODED in build flags!
```

**CONFLICT:** Version file says `0.0.00-3`, but build hardcodes `0.0.14`. Which is correct?

### V1 platformio.ini
```
Default env: heltec_wifi_lora_32_V3  ← DEFAULT IS V3 NOT V2
Uses: SUPPORT_* flags (modular approach)
```

**PROBLEM:** V1 defaults to V3 hardware, but V2 code is supposed to be for V2/V3/V4 unified.

---

## 3. FIRMWARE DIRECTORIES

```
firmware/
├── v1/                    ← "ACTIVE DEVELOPMENT" per CLAUDE.md (but broken)
├── v1_fix/                ← ABANDONED FIX DIRECTORY? (why not merged to v1?)
├── v2/                    ← "TEST BED" per CLAUDE.md (uncommitted changes!)
├── v4-factory-test/       ← NEW (untracked)
└── heltec-diagnostics/    ← HELTEC EXAMPLES (not ours)
```

**PROBLEM:** v1_fix exists but v1 problems aren't fixed. v2 has uncommitted changes.

---

## 4. UNCOMMITTED CHANGES (as of 2026-03-26 15:45)

### AG's Firmware Changes (v2 only — NOT COMMITTED)
```
firmware/v2/lib/App/boot_sequence.cpp         ← I2C mutex fixes
firmware/v2/lib/App/command_manager.cpp       ← Added VSTATUS
firmware/v2/lib/App/control_loop.cpp          ← ?
firmware/v2/lib/App/oled_manager.cpp          ← OLED hardening
firmware/v2/lib/App/power_manager.cpp         ← VEXT pulse sequence
firmware/v2/lib/App/status_builder.cpp        ← New status commands
firmware/v2/lib/HAL/mcp_hal.cpp               ← MCP changes
firmware/v2/lib/HAL/mcp_manager.cpp           ← ?
firmware/v2/lib/Transport/mqtt_transport.cpp  ← MQTT changes
firmware/v2/lib/Transport/mqtt_transport.h    ← MQTT header changes
firmware/v2/platformio.ini                    ← Config changes
firmware/v2/docs/AGENT_RADIO.md               ← Status notes
```

**BLOCKER:** AG's firmware work is 100% uncommitted. Tests can't validate it.

### My Work (ON FEATURE/HYBRID-PROXY-WEBAPP-INTEGRATION BRANCH)
```
docs/plans/2026-03-26-loralink-assistant-design.md  ← ✅ Committed (4 commits)
tools/multi-agent-framework/                        ← ✅ Committed (Orion framework)
docs/plans/2026-03-26-loralink-assistant-design.md  ← ✅ Committed (Branding)
docs/plans/2026-03-26-loralink-assistant-design.md  ← ✅ Committed (Ollama offload)
```

**PROBLEM:** My work is on a FEATURE BRANCH, not merged to main. 4 commits behind.

### Untracked New Files
```
firmware/v4-factory-test/                 ← NEW (AG created?)
tools/assistant/                          ← NEW (from my spec)
tools/discover_fleet.py                   ← NEW (what is this?)
tools/fleet_deploy.ps1                    ← NEW (deployment script)
docs/CLAUDE_ORIENTATION.md                ← NEW (what is this?)
firmware/v2/data/telemetry_schema.json    ← NEW (schema file)
tools/webapp/static/telemetry_registry.json ← NEW (registry)
```

**PROBLEM:** New files created but not staged or committed.

---

## 5. WHAT'S ACTUALLY BEING BUILT?

### V1 (per CLAUDE.md: "Active Development")
- **Status:** Supposedly stable at v0.3.0 (from commit 62ec505)
- **Recent work:** Multiple "restore v1 stability" commits
- **Current state:** NOT VERSIONED. Version file shows 0.0.00-2
- **Buildable?** ✅ Unknown (last stable was v0.3.0)

### V2 (per CLAUDE.md: "Test Bed")
- **Status:** Unstable, lots of regressions (I2C deadlocks, VEXT polarity, OLED init)
- **Recent work:** Decomposition into Boot/Control/Handler modules
- **Uncommitted changes:** 11 files
- **Current state:** NOT VERSIONED. Version file shows 0.0.00-3. Hardcoded 0.0.14 in build.
- **Buildable?** ❌ Probably broken (uncommitted changes)

### V3
- **Status:** Unknown. No dedicated v3/ directory. V1 defaults to V3 hardware.
- **Current state:** NOT VERSIONED. Version file shows 0.0.00-3.
- **Buildable?** ❓ Unclear if V1 code works on V3 or if it's V2 code.

### V4
- **Status:** Only truly versioned (0.2.82-4)
- **Recent work:** GPS/mesh fixes, factory test
- **New files:** firmware/v4-factory-test/ (untracked)
- **Buildable?** ✅ Maybe (has actual version)

---

## 6. WHAT CLAUDE BUILT (MY WORK)

### Orion Framework
- ✅ Committed to feature/hybrid-proxy-webapp-integration
- Generalized multi-agent cooperative development with RAG
- Lives at `tools/multi-agent-framework/`
- **Integration with firmware:** NONE YET

### LoRaLink Assistant Spec
- ✅ Committed to feature/hybrid-proxy-webapp-integration
- Production spec for FastAPI + system tray + WebSocket chat UI
- Lives at `docs/plans/2026-03-26-loralink-assistant-design.md`
- **Integration with firmware:** Expects REST API from daemon (not specified which firmware)

### Orion's Garden
- ✅ Standalone project at C:\Users\spw1\Documents\Garden\
- NOT in Antigravity repo
- Works with Orion framework
- **Integration with firmware:** NONE

### Branding Overhaul
- ✅ Committed to feature/hybrid-proxy-webapp-integration
- Made all branding config-driven (app_name, tagline, copyright, colors)
- Applied to Orion framework + all configs
- **Integration with firmware:** Affects UI/OLED only (not yet in v2)

---

## 7. WHAT AG BUILT (FIRMWARE WORK)

### Uncommitted V2 Changes
- I2C mutex deadlock fixes (boot_sequence.cpp)
- VSTATUS command (command_manager.cpp)
- OLED hardening (oled_manager.cpp, 100ms delays)
- VEXT pulse sequence standardization (power_manager.cpp: LOW-HIGH-LOW)
- Status builder refactoring (status_builder.cpp)
- MCP HAL/manager changes
- MQTT transport changes
- **Status:** ❌ NOT COMMITTED, NOT TESTED, NOT VERSIONED

### Telemetry Protocol Addition
- Added to docs/AGENT_RADIO.md (2026-03-26)
- STATUS vs VSTATUS split (friendly vs verbose)
- **Status:** ✅ Documented, but not in code

### V4 Factory Test
- New directory: firmware/v4-factory-test/
- **Status:** ❓ Purpose unclear, untracked

---

## 8. INTEGRATION PROBLEMS

| Component | Where It Is | Where It Needs To Be | Status |
|-----------|-------------|----------------------|--------|
| Orion Framework | feature/hybrid-proxy-webapp-integration | main | ❌ Unmerged |
| Assistant Spec | feature/hybrid-proxy-webapp-integration | main | ❌ Unmerged |
| V2 Firmware Fixes | Uncommitted in v2/ | Committed to main | ❌ Untracked |
| V4 Factory Test | firmware/v4-factory-test/ (new) | ??? | ❓ Unclear purpose |
| Branding Overhaul | feature/hybrid-proxy-webapp-integration | main | ❌ Unmerged |
| Tools (fleet deploy, discover) | Untracked | ??? | ❓ Unclear purpose |

---

## 9. CRITICAL QUESTIONS UNANSWERED

1. **Which firmware version should AG focus on?** V1? V2? V3? V4?
2. **What does "active development" actually mean?** (per CLAUDE.md, V1 is, but it's broken)
3. **Why does the version file NOT match the build flags?** (.version says 0.0.00-3, platformio.ini says 0.0.14)
4. **How do I know what "stable" means?** Last stable V1 was v0.3.0 (from Feb 25). Current state is unknown.
5. **Where should AG's firmware changes go?** V1? V2? Both?
6. **What is firmware/v4-factory-test/ for?** Manufacturing? Testing? Deployment?
7. **Why do we have v1/ and v1_fix/?** Why not merge the fixes back?
8. **Should my Orion framework/specs be built into firmware?** Or are they just tools?

---

## 10. RECOMMENDATIONS FOR CLARITY

🛑 **STOP everything and do this:**

1. **Merge all firmware work to main** — uncommitted v2 changes need to land
2. **Pick ONE firmware version** as the source of truth (V1? V2?)
3. **Fix the version file** — decide if it's 0.0.00-3 or 0.0.14, make them match
4. **Merge feature branches** — Pick which branch becomes the next main, merge it, clean up
5. **Document what each version is for:**
   - V1: ? (Active dev per CLAUDE.md, but unstable)
   - V2: ? (Test bed per CLAUDE.md, but uncommitted changes)
   - V3: ? (No dedicated directory, unclear)
   - V4: ? (Only versioned, factory test unclear)
6. **Clarify the release plan:**
   - What's "shipping" next? V1? V2? V4?
   - Who/what validates it? AG? Tests? Manual QA?
7. **Integrate my work** — Orion framework + specs either into firmware build process, or keep separate

---

**CONCLUSION:** The repo is at a **critical junction**. Too many branches, unversioned firmware, uncommitted changes, and unclear objectives. Before building anything new, we need to **consolidate and clarify what's actually shipping.**

