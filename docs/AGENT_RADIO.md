# AGENT_RADIO — Three-Agent Coordination
**Status:** FULL SPEED AHEAD — Product Ready to Ship
**Team:** Claude + Local Model (Ollama) + AG
**Updated:** 2026-03-26 14:45

---

## Agentic Development Model

```
Claude (Reasoning)          Local Model (Execution)      AG (Hardware)
├─ Plan & decide           ├─ Code generation           ├─ Flash devices
├─ Architecture            ├─ Search/replace            ├─ Test real hardware
├─ High-level strategy     ├─ Repetitive tasks          ├─ Validate physical behavior
└─ Daemon/provisioning     └─ 24/7 async processing     └─ Report findings
```

**All three work in parallel. No blocking. Shipping product.**

---

## Current Status: Phase 0 Complete ✅

**Hardware Validation Results:**
- ✅ V2 (Node 30): Boot stable, GPIO 21 conflict resolved, 10min uptime clean
- ✅ Webserver removed from V2 (smaller footprint)
- ✅ WiFi kept (MQTT, OTA, daemon connectivity)
- ✅ All variants build clean (V2, V3, V4)
- ✅ Ready for Phase 50

**Next:** Phase 50 (Autonomous Mesh) & Magic Assistant (Orion)

---

## Daily Workflow (Locked Pattern)

### 09:00 PLAN
**Claude:**
- What's blocking the product?
- What needs to ship today?

**AG:**
- What hardware validation is critical?
- What's the test plan?

**Local Model:**
- What async tasks can start? (code gen, search/replace, analysis)

**Action:** Queue async tasks immediately (don't wait)

---

### 10:00 DECIDE
**Claude:** Proposes daemon/architecture changes
**AG:** Proposes hardware tests and feedback
**Both:** Agree on partition (who does what, what's off-limits)

**Action:** Queue more async tasks if needed

---

### 11:00 IMPLEMENT
**Claude:** Code daemon features (parallel with AG)
**AG:** Test hardware (parallel with Claude)
**Local Model:** Processing queue in background (no one waits)

**Key:** Three independent work streams. No blocking.

---

### 14:00 DEPLOY
**All:** Push code to main if builds clean
**Local Model:** Results from queue are ready

**Action:** Integrate local model outputs into codebase

---

### 15:00 TEST
**Claude:** Test daemon REST API against real expectations
**AG:** Test hardware behavior against real devices
**Both:** Validate integration

**Action:** Document surprises and learnings

---

### 17:00 RELEASE
**All:** If all tests pass → tag version and ship
**Action:** Queue next batch of async tasks for tomorrow

---

## Local Model Integration (24/7 Hybrid Architecture)

**Ollama (Local)** + **OpenRouter (Cloud Fallback)**

### Hybrid Model Proxy
Intelligent routing via `tools/hybrid_model_proxy.py`:
- ✅ **Prefer Local**: Routes to Ollama ($0 cost) if healthy.
- ✅ **Cloud Fallback**: Routes to OpenRouter (Claude 3/GPT-4) if Ollama is down.
- ✅ **Cost Tracking**: Per-request token and USD metrics logging.

### Workflow Example
```python
# Query with local preference
result = await proxy.query(
    model="qwen2.5-coder:14b",
    prompt="Generate mesh relay logic...",
    prefer_local=True
)
```

**Benefits:**
- ✅ **90%+ Cost Savings**: Primary execution on local GPU.
- ✅ **High Availability**: Cloud fallback ensures no workflow blocking.
- ✅ **Unified Interface**: One proxy, multiple backends.

---

## Phase 50: Autonomous Mesh Sovereignty

**Goal:** Mesh-enabled GPIO control across devices

**What it does:**
1. Device A sends: "Toggle relay on Device B, pin 32"
2. Mesh finds Device B (MAC-seeded crypto, peer discovery)
3. Device B executes: toggle pin 32
4. Device B replies: "Done, relay is on"
5. User sees result in webapp

**Owner:** Claude (daemon) + AG (firmware validation)

**Status:** ✅ **RESTORED — FULL SPEED AHEAD**
- ✅ **EMERGENCY RECOVERY COMPLETE:** I2C Mutex deadlocks resolved.
- ✅ **STABILITY:** Non-blocking UI refactor verified across V2/V3/V4 fleet.
- ✅ **HARDWARE:** V1-style pulse sequence (LOW-HIGH-LOW) standardized for power-rail priming.
- ✅ **RESPONSIVE:** Loop frequency (100Hz) maintained during OLED refreshes.

---

## Tasks for Today (2026-03-26) — Phase 50 Autonomous Mesh

| Finalize Phase 50 Mesh Specs | Claude | ✅ Done |
| Implement ControlPacket Parser | AG | ⏳ Next |
| Update Daemon to MAC-Primary | Claude | ⏳ Next |
| Scaffold Magic Assistant (P1) | AG + Ollama | ⏳ Next |

**⚡ AG: Offload boilerplate to Ollama.** Don't hand-write CSS, SQLite CRUD, route stubs, test scaffolds, or repetitive patterns. Queue them to qwen2.5-coder:14b via the hybrid proxy — it's free and running 24/7. You decide architecture, Ollama generates code, you review and integrate. See `docs/plans/2026-03-26-magic-assistant-design.md` → "IMPERATIVE: Offload to Ollama" section for specifics.

---

## 🛠️ Verification Results (2026-03-26)
- ✅ **V2 (COM7)**: Restored (Visual + Reactive) -> v0.0.12
- ✅ **V3 (COM17)**: Restored (Visual + Reactive) -> v0.0.12
- ✅ **V4 (COM19)**: Restored (Visual + Reactive) -> v0.0.12
- ✅ **MESH**: 3+ nodes actively beaconing on v0.0.12 baseline.

---

## Sign-Off Checklist (Minimal)

**Before Phase 50 implementation:**
- [✅] V2 hardware validation complete
- [✅] Webserver removed, WiFi kept
- [✅] Ollama local model integrated
- [✅] Async queue workflow tested
- [ ] Phase 50 daemon API spec locked
- [ ] AG confirms firmware scope

**Once all checked:** Ship Phase 50

---

## Quick Reference

| Component | Status | Owner |
|-----------|--------|-------|
| V0.0.12 baseline | ✅ Stable | Both |
| Phase 0 (device simplification) | ✅ Complete | Both |
| Phase 50 (mesh sovereignty) | 🔄 In progress | Both |
| Hybrid Model Proxy | ✅ Operational | User |
| Product ready to ship | ✅ Yes | All |

---

## Principles

1. **Ship working code** — v0.0.11 works, Phase 50 ships soon
2. **Parallel work** — Claude, Local Model, AG all independent
3. **Real product feedback** — Test on actual hardware
4. **Async processing** — Local model works 24/7, no blocking
5. **Incremental refinement** — Refactor based on real evidence

---

## How to Use This Document

**For Claude:**
1. Plan high-level strategy
2. Queue async tasks to local model (don't wait for results)
3. Continue working on daemon
4. Check model results at DEPLOY phase

**For AG:**
1. Validate hardware in parallel with Claude coding
2. Report findings and constraints
3. Test Phase 50 mesh when ready
4. Feedback loops fast

**For Local Model:**
1. Process queued tasks in background
2. Results ready when needed
3. No blocking anyone

**For User:**
1. Monitor status
2. Review daily learnings
3. Make decisions on blockers
4. Ship the product

---

---

## Telemetry Protocol & Agent Management (v0.1.0)

To minimize dashboard latency and mesh overhead, the fleet implements a tiered telemetry protocol.

### Tiered Commands
1. **`STATUS` (Friendly/UI)**:
   - **Fields**: `name`, `ver`, `ip`, `bat_pct`, `mode`, `vext`, `lora_rssi`, `peer_cnt`, `uptime`, `gps`.
   - **Usage**: Periodic heartbeats, general dashboard rendering, and mobile app polling.
   - **Agent Management**: External agents (Claude/Orion) should prefer this for 90% of interactions.

2. **`VSTATUS` (Verbose/Technical)**:
   - **Fields**: Fullforensics including `mac`, `hw_id`, `lora_snr`, `rssi_history`, `heap`, `reset`, `relay_mask`, and GPS diagnostics.
   - **Usage**: Deep telemetry analysis, debugging, and initial provisioning verification.
   - **Agent Management**: Trigger only on diagnostic failure, manual "Expert" toggle, or fleet audit events.

---

**Next Sync:** 2026-03-27 09:00 (Phase 50 planning)
**Goal:** Ship Phase 50 by end of week
**Motto:** Product ready. Agentic development. Full speed ahead.
