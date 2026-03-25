# Branch Status & Merge Strategy
**Last updated:** 2026-03-25 by Claude

## Current State

```
main (cfb7a8f) ✅ STABLE
├─ Provisioning daemon + webapp
├─ System tray icon support
├─ HTTP `/api/provision` endpoint
└─ BUILDS SUCCESSFULLY (all V2/V3/V4 variants)

feature/v2-rationalization (da53160) ⚠️ CONFLICTS
├─ 6 commits ahead of main
├─ Adds tray icon + fleet admin updates
└─ Auto-merge FAILS (incompatible firmware implementations)

feature/v4-gps-mesh-fix (da53160) ⚠️ CONFLICTS
├─ 6 commits ahead of main (same as v2-rat after rebase)
└─ Auto-merge FAILS (incompatible firmware implementations)
```

## The Problem

The three branches have **conflicting implementations** of the same firmware modules:
- `oled_manager.cpp`: Different refactoring approaches
- `command_manager.cpp`: Different StatusBuilder integration
- `status_builder.cpp`: Different API (e.g., `getNumericNodeID()`)
- `http_api.cpp`: Different endpoint routing

## What's on Main (KEEP THIS)

✅ **Provisioning system** (daemon + webapp)
✅ **System tray icon** with status overlays
✅ **Fleet admin webapp** enhancements
✅ **172.16.0.x network** configuration
✅ **BUILDS SUCCESSFULLY**

## What's on the Branches (NEEDS REVIEW)

❓ Feature/v2-rationalization:
- Modular boot/control/handler split
- NVS feature registry consolidation
- Plugin manager architecture
- **Status:** Builds individually, conflicts with main when merged

❓ Feature/v4-gps-mesh-fix:
- GPS manager isolation
- Mesh saturation fixes
- Serial1 handling
- **Status:** Behind main, conflicts when rebased

## Recommended Next Steps

### Option A: Keep Main Clean (RECOMMENDED)
1. **Main stays at cfb7a8f** (working provisioning system)
2. **AG reviews** what should actually go in (cherry-pick only the non-conflicting GPS/mesh fixes)
3. **Create detailed PR** from AG explaining each change
4. **Merge carefully** with manual conflict resolution

### Option B: Full Rebuild (If you want the comprehensive V2 refactor)
1. Start a new branch `feature/v2-complete` from main
2. Copy only the **non-firmware** parts from branches (tray, webapp)
3. Manually integrate **firmware changes** piece-by-piece
4. Test after each piece
5. Create single comprehensive PR

## For AG

**Claude has stopped the merges** to keep main stable.

**Your task:** Review `feature/v2-rationalization` and `feature/v4-gps-mesh-fix`:
- Which firmware changes are actually needed?
- Which can be dropped (already done better on main)?
- What's the minimal set to cherry-pick?

Then post on AGENT_RADIO with:
```
[PROPOSAL] Merge Strategy for V2/V4 branches

Changes to cherry-pick:
- GPS isolation (reason: fixes Serial1 conflicts)
- Mesh discovery (reason: prevents saturation)

Changes to drop:
- OLED refactor (reason: main version is compatible)
- Command routing (reason: main is simpler + works)

Request: Review attached diff before merge
```

---

**Status:** 🟢 Main is stable and ready for deployment
**Next:** Await AG's technical review
**Timeline:** No urgency (provisioning system works now)
