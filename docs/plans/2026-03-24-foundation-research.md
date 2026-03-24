# Foundation Research: Flashing Reliability & Plugin Architecture Roadmap

**Date:** 2026-03-24
**Status:** RESEARCH TASK (Not Implementation)
**Requestor:** User
**Context:** User wants to design modular, plug-and-play hardware with flexible firmware configuration. Before designing the plugin/configuration layer, we need to understand the **flashing/OTA reliability** and **existing firmware state** to build on solid ground.

---

## Part 1: Current State Audit

### AG's V2 Rationalization Progress (Feature Branch: `feature/v2-rationalization`)

**Completed:**
- ✅ **Phase 1** (Commit `66e35c6`): Deleted dead code (`display_manager`, `sim_runner`, NVS legacy)
- ✅ **Phase 2** (Commit `7cf25c1`): Consolidated `NVSConfig` into `NVSManager` with ironclad key migration
  - Key insight: `hw_ver` (uint8) vs `hw_version` (string) distinction is critical for field device data
  - Boot count + reset reason auto-captured for robustness
  - Crypto key uses binary blob internally, hex for I/O

**Next Phases (Planned but not started):**
- Phase 3: `main.cpp` decomposition (architecture rework)
- Phase 4: CommandRegistry cleanup
- Phase 5: Plugin system expansion

**Build Status:** V2 ✅ V3 ✅ V4 ✅ (all passing after Phase 2)

---

### Flashing/OTA Workflow (Current)

**USB Flash (for local testing):**
```
pio run -e heltec_wifi_lora_32_V3 --target upload
```

**OTA Flash (for fleet):**
```
pio run -e ota_slave --target upload    # to 172.16.0.26
pio run -e ota_master --target upload   # to 172.16.0.27
```

**Observations from Docs:**
- OTA environments target static IPs (hardcoded)
- No mDNS-based discovery mentioned in workflows
- Sequential flash (no parallelization)
- No built-in retry logic or rollback mechanism

---

## Part 2: Reliability Concerns to Investigate

### 🔴 Known Issues (from recent commits)

1. **V4 Button Crash** — Fixed in commit `05172ec` ("stabilize V4 button crash")
   - Suggests V4 variant has unique boot/handling requirements
   - May indicate fragile boot sequencing still present

2. **Brownout Sensitivity** — Staggered boot mentioned in ARCHITECTURE_MAP
   - Radio + WiFi + OLED simultaneous power-up causes spikes
   - Mitigation: deliberate `delay()` calls in boot sequence
   - **Question:** Are delays adequate? Is there a pattern for Brown Out Detection (BOD)?

3. **mDNS/Web Conflicts** — Fixed in commit `05172ec`
   - Suggests WiFi manager and display manager had contention
   - May indicate task scheduling or I2C bus contention (OLED uses same bus as future expanders)

4. **Serial Cache Lookup Bug** — Fixed in commit `36f7dea`
   - Transport routing was using UUID instead of port identifier
   - Relevant: OTA flashing goes over same transport layer

### 🟡 Questions to Answer

**Flashing:**
- [ ] OTA upload: What causes timeouts? Are there retry mechanisms?
- [ ] Rollback: If OTA fails mid-flash, what's the recovery path?
- [ ] Boot validation: After OTA, does device validate firmware CRC before committing?
- [ ] Persistence: Are NVS settings preserved across OTA updates?

**Hardware:**
- [ ] Is brownout detection enabled in ESP-IDF config?
- [ ] What's the minimum stable boot delay sequence for V3 vs V4?
- [ ] MCP23017 + OLED share I2C (Wire) — any contention issues observed?

**Firmware:**
- [ ] Current code: Is there a watchdog timer active?
- [ ] Boot recovery: If device crashes during flashing, can it self-recover?
- [ ] Version validation: Does firmware validate its own version before running?

---

## Part 3: Plugin Architecture Dependencies

### ✅ Prerequisites for Modular Configuration System

**Must have BEFORE designing plugin/config layer:**

1. **Stable OTA** — Users should be able to flash firmware updates without risk of bricking
   - Implies: CRC validation, rollback capability, watchdog timer
   - Current status: ❓ Unknown

2. **Reliable Boot Sequence** — Device must boot consistently regardless of hardware configuration
   - Implies: Proper staggered power-up, V3/V4 variant handling
   - Current status: ⚠️ V4 button crash was recent issue (possibly fixed)

3. **I2C Bus Isolation** — Wire bus (OLED) must not interfere with Wire1 (expanders)
   - Implies: No `Wire.begin()` re-initialization after `Heltec.begin()`
   - Current status: ✅ Known and documented in `heltec-loralink` skill

4. **Persistent Configuration** — NVS must reliably store device registry and user config
   - Implies: Atomic writes, wear leveling, CRC protection
   - Current status: ✅ Phase 2 consolidated to authoritative `NVSManager`

5. **Plugin Lifecycle** — Plugins must initialize in correct order without hard dependencies
   - Implies: Boot-time dependency graph, error isolation
   - Current status: ⚠️ Partially implemented (exists but may be fragile)

---

## Part 4: Proposed Research & Validation Plan

### Phase A: Flashing Stability Audit (1-2 sessions)

**What to investigate:**
1. **OTA mechanism:**
   - [ ] Read `WiFiManager::handleOTA()` — how does it write to flash?
   - [ ] Check: Is there CRC validation before commit?
   - [ ] Check: If OTA fails, can device recover?

2. **Boot sequence robustness:**
   - [ ] Measure actual current draw during boot (V3 vs V4)
   - [ ] Test: Remove all `delay()` calls — at what point does brownout occur?
   - [ ] Test: Does watchdog timer (if enabled) prevent boot hang?

3. **NVS persistence across OTA:**
   - [ ] Flash firmware update → verify NVS config survives
   - [ ] Simulate dirty shutdown during OTA → check recovery

**Success criteria:**
- OTA flash failure does NOT corrupt device state
- Device can recover from incomplete OTA without serial intervention
- NVS config persists across firmware updates

---

### Phase B: Hardware Configuration Discovery (1 session)

**What to investigate:**
1. **I2C bus availability:**
   - [ ] Wire (SDA=17, SCL=18): Confirm OLED is only user
   - [ ] Wire1: What pins can be used? Are there conflicts?
   - [ ] Multi-expander: Test daisy-chaining 3+ MCP23017s

2. **GPIO pin safety:**
   - [ ] Map all "free" GPIO per V3/V4 variant
   - [ ] Verify: Pin 14 is truly unusable on V3/V4 (LoRa DIO1)
   - [ ] Verify: No pin conflicts between radio and relays

3. **Boot order dependency:**
   - [ ] What if I2C expander is not present at boot?
   - [ ] Can plugins gracefully skip missing hardware?

**Success criteria:**
- Clear "safe pins" matrix for user assignments
- I2C expander auto-discovery working on both Wire and Wire1
- Plugins tolerate missing hardware without crashing boot

---

### Phase C: Plugin System Assessment (1 session)

**What to investigate:**
1. **Current plugin lifecycle:**
   - [ ] Are plugins registered at compile-time or runtime?
   - [ ] What happens if `init()` fails?
   - [ ] Do plugins have access to device registry?

2. **Configuration flow:**
   - [ ] How does `configure(JsonObjectConst)` get called?
   - [ ] Can plugins read from NVS during `init()`?
   - [ ] Can plugins update their own config after initialization?

3. **Pin abstraction readiness:**
   - [ ] Is there a virtual pin layer today?
   - [ ] Can DHT22Plugin work with GPIO4 from config instead of hardcoded?

**Success criteria:**
- Plugin system is extensible without firmware recompile
- Plugins can read NVS config at init time
- Device registry is accessible to all plugins

---

## Part 5: Roadmap (Pending Research Results)

### IF flashing/boot are stable (Phase A succeeds):
→ Proceed to **modular plugin architecture** (design in next session)

### IF flashing/boot need hardening (Phase A has findings):
→ **STOP plugin work** → Fix boot/OTA → Revalidate → Then plugin design

### IF I2C discovery needs work (Phase B has blockers):
→ Implement **I2C bus auto-discovery** module before plugin config layer

### IF plugin system is fragile (Phase C has concerns):
→ Refactor plugin lifecycle (using `superpowers:writing-plans`) before exposing to users

---

## Part 6: Key Documents Created

| Document | Purpose | Status |
|----------|---------|--------|
| `heltec-loralink` skill | Hardware reference for Heltec V2/V3/V4 | ✅ Complete |
| This file | Research roadmap | ✅ Created |
| TBD: Flash Stability Report | Findings from Phase A | ⏳ Pending |
| TBD: Hardware Config Report | I2C/GPIO mapping from Phase B | ⏳ Pending |
| TBD: Plugin Architecture Design | Modular plugin spec (Phase C outcome) | ⏳ Pending |

---

## Part 7: Skill Recommendations

**For this research task:**
- Invoke `superpowers:systematic-debugging` when investigating flashing failures
- Invoke `electronics-design-esp32` for brownout/power analysis
- Invoke `loralink-production-monitoring` for reliability patterns

**For plugin architecture (after research):**
- `superpowers:brainstorming` → design modular system
- `superpowers:writing-plans` → multi-phase implementation
- `heltec-loralink` skill → hardware specifics during implementation

---

## Next Steps

1. **User/AG:** Approve this research roadmap or modify focus areas
2. **Claude:** Execute Phase A (Flashing Stability Audit)
3. **Claude:** Document findings and any blockers
4. **Both:** Decide whether to proceed with plugin architecture or address blockers first
