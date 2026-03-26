# AGENT_RADIO — Three-Agent Coordination
**Status:** FULL SPEED AHEAD — Product Ready to Ship
**Team:** Claude + Local Model (Ollama) + AG
**Updated:** 2026-03-26 14:30

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

**Next:** Phase 50 (Autonomous Mesh) — Full speed ahead

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

**Status:**
- ✅ Spec finalized (AG's proposal)
- ✅ Local model can generate helper functions async
- ⏳ Implementation starts tomorrow

---

## Tasks for Today (2026-03-26)

| Task | Owner | Status |
|------|-------|--------|
| Queue Phase 50 helper functions | Claude → Local Model | ⏳ Now |
| Hardware validation complete | AG | ✅ Done |
| Update AGENT_RADIO | Claude | ✅ Done |
| Plan Phase 50 daemon API | Claude | ⏳ Next |
| Finalize mesh spec | AG | ⏳ Parallel |

---

## Tasks for Tomorrow (2026-03-27)

### PLAN (09:00)
- Phase 50 implementation start
- Daemon API endpoints
- Hardware integration points

### DECIDE (10:00)
- Claude: Device ↔ Daemon MQTT contract (qwen2.5-coder can generate schema)
- AG: Firmware changes needed for Phase 50
- Local Model: Queue API client generation async

### IMPLEMENT (11:00)
- Claude: Daemon REST/MQTT endpoints
- AG: Firmware mesh integration
- Local Model: Generate boilerplate (async, results ready in queue)

### DEPLOY (14:00)
- Build verification (all variants)
- Integrate local model outputs

### TEST (15:00)
- Hardware validation (Phase 50 mesh)
- API testing

### RELEASE (17:00)
- v0.1.0-phase50 if tests pass

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
| V0.0.11 baseline | ✅ Stable | Both |
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

**Next Sync:** 2026-03-27 09:00 (Phase 50 planning)
**Goal:** Ship Phase 50 by end of week
**Motto:** Product ready. Agentic development. Full speed ahead.
