# Multi-Agent Workflow — Complete System Guide

This document ties together the complete local-first, multi-agent development system for LoRaLink and NutriCalc.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Your PC (Source of Truth)                                       │
│  ├─ ~/loralink/ (local workspace)                                │
│  ├─ ~/nutricalc/ (local workspace)                               │
│  └─ ~/backups/ (automatic session backups)                       │
└─────────────────────────────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────────────────────────────┐
│  Git + Lock Files (Multi-Agent Coordination)                     │
│  ├─ .locks/ directory (file-based agent locks)                  │
│  ├─ .gitignore (ignores .locks/ and ~/backups/)                 │
│  └─ session-commit.py (saves work safely)                        │
└─────────────────────────────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────────────────────────────┐
│  GitHub (Backup / Archive)                                       │
│  ├─ https://github.com/thynk3rbot/loralink                      │
│  ├─ https://github.com/thynk3rbot/nutricalc                     │
│  └─ Consolidation happens on VERSION CHANGE                      │
└─────────────────────────────────────────────────────────────────┘
```

**Key Principle:** PC is source of truth. GitHub is only for backups and coordination points.

---

## Component Ownership

| Agent | Primary Responsibility | Lock File | Files |
|---|---|---|---|
| **Claude** | LoRaLink firmware core managers, command routing, radio stack | `.locks/claude.lock` | `src/managers/`, `src/config.h`, `src/crypto.h` |
| **Antigravity** | NutriCalc server, MQTT integration, solver algorithm | `.locks/antigravity.lock` | `tools/webapp/server.py`, `tools/webapp/static/`, `INTEGRATION.md` |
| **Codex** | Firmware optimizations, performance tuning, watchdog | `.locks/codex.lock` | `src/main.cpp`, `src/managers/Performance*.`, `src/managers/Power*.` |

---

## Session Workflow (Step-by-Step)

### Before You Start Working

**1. Pull Latest**
```bash
cd ~/loralink
git pull origin main

cd ~/nutricalc
git pull origin main
```

**2. Check Status**
```bash
python3 ~/session-status.py
```
Shows you:
- Current branch
- Latest commit hash
- Uncommitted changes
- Sync status with GitHub

**3. For Your Assigned Components:**

If you're Claude and want to work on BLEManager:
```bash
cd ~/loralink
python3 agent-tracking.py acquire Claude "Refactoring BLE GATT response callback"
```

This creates `.locks/claude.lock` with:
- Agent name (Claude)
- Timestamp (2026-03-09T14:30:00Z)
- Session ID (session-20260309-1430)
- Task description

### During Your Session

**Work normally:**
- Edit files in your assigned directories
- Test locally
- Make incremental git commits if desired (optional)
- Don't touch files assigned to other agents

**Check status anytime:**
```bash
python3 agent-tracking.py status
```

Shows:
- Active locks (which agents are working)
- Which files have been modified
- Who owns each modified file
- Conflict detection

### End of Session (Critical!)

**1. Use session-commit.py**
```bash
python3 ~/session-commit.py
```

This automatically:
- Shows what changed
- Asks for confirmation
- Creates timestamped commit
- Backs up to `~/backups/loralink-TIMESTAMP/`
- (Optional) Pushes to GitHub

**2. Release Your Lock**
```bash
cd ~/loralink
python3 agent-tracking.py release Claude
```

**3. Check Final Status**
```bash
python3 ~/session-status.py
```

---

## Version Management System

### Current Version

Stored in `src/config.h`:
```c
#define FIRMWARE_VERSION "v0.0.1"
```

**Starting point:** v0.0.1 (as of this session)

### When Version Changes

**Automatic trigger:** When you upload firmware via PlatformIO:
```bash
pio run -t upload
# Firmware uploads with v0.0.1
```

**After upload**, a post-build hook (TODO: implement) should:
1. Read `src/config.h` → get current version
2. Parse semantic version: `v0.0.1` → `[0, 0, 1]`
3. Increment patch: `[0, 0, 2]`
4. Write back: `#define FIRMWARE_VERSION "v0.0.2"`
5. Auto-commit: `"fw: auto-bump v0.0.1 → v0.0.2"`

### Consolidation on Version Change

When version in `src/config.h` changes (e.g., v0.0.1 → v0.0.2):

**Manually trigger consolidation:**
```bash
cd ~/loralink
python3 merge-to-github.py detect           # Check if version changed
python3 merge-to-github.py consolidate       # Consolidate all agent work
python3 merge-to-github.py --auto-push       # Consolidate AND push
```

**What happens:**
1. Detects version change from git history
2. Gathers all modified files by agent
3. Creates single consolidation commit like:
   ```
   consolidate: v0.0.1 → v0.0.2 (all agents)

   Agent contributions:
     Claude: 5 file(s) modified
       • src/managers/BLEManager.cpp
       • src/managers/CommandManager.h
       ... and 3 more
     Antigravity: 2 file(s) modified
       • tools/webapp/server.py
       ... and 1 more
     Codex: 3 file(s) modified
       • src/main.cpp
       ... and 2 more

   Lock files cleared.
   ```
4. Clears all `.locks/` files
5. (Optional) Pushes to GitHub `main`

---

## Discrete Web Page Timestamps

Each web page in the LoRaLink webapp will have a subtle timestamp showing when it was built.

**Where to look:** Bottom-right corner of each page (very small, low contrast)

**Format:** `Build: 2026-03-09T14:30:00Z` (ISO 8601)

**Implementation in server.py:**
```python
# At app startup
APP_BUILD_TIMESTAMP = datetime.utcnow().isoformat() + "Z"

@app.get("/")
async def serve_index():
    with open("static/index.html") as f:
        html = f.read()
    # Inject timestamp before closing </body>
    html = html.replace(
        "</body>",
        f'<span class="build-timestamp">Build: {APP_BUILD_TIMESTAMP}</span></body>'
    )
    return HTMLResponse(html)
```

**CSS (in shared.css):**
```css
.build-timestamp {
    position: fixed;
    bottom: 4px;
    right: 4px;
    font-size: 8px;
    color: rgba(255, 255, 255, 0.15);
    font-family: monospace;
    opacity: 0.3;
    pointer-events: none;
}
```

This way:
- Timestamp is always fresh (generated when server starts)
- Never synced to git (not saved to file)
- Visible only if you know to look
- Indicates when each page was last served

---

## Lock File Lifecycle

### Creation
```
Agent starts work → python3 agent-tracking.py acquire <name> "<task>"
↓
Creates: .locks/claude.lock (example)
Contains: agent name, timestamp, session ID, task description
```

### Active
```
Agent works on files → .locks/claude.lock remains
↓
Other agents can see lock and know Claude is working
Lock prevents accidental overwrites
```

### Timeout
```
Lock file exists for >2 hours → considered abandoned
↓
Manual cleanup: rm .locks/claude.lock (or let merge-to-github.py do it)
```

### Release
```
Agent finishes work → python3 agent-tracking.py release Claude
↓
Removes: .locks/claude.lock
Lock file gone, agent is "offline"
```

---

## Preventing Conflicts: Decision Tree

```
Before modifying a file:
│
├─ Is it in my assigned directory? (See AGENT_ASSIGNMENTS.md)
│  │
│  ├─ YES → Check if my lock exists
│  │  │
│  │  ├─ NO lock yet → Create it: agent-tracking.py acquire <name> "<task>"
│  │  │
│  │  └─ Lock exists → Good, I'm "locked in", proceed
│  │
│  └─ NO → Do NOT modify it
│     │
│     └─ Instead: Create issue/comment for assigned agent
│
└─ Modify, test, commit locally
   │
   └─ End of session → session-commit.py + agent-tracking.py release
```

---

## Emergency Procedures

### Lock File Stuck (Agent Abandoned Work)

```bash
# Check lock age
ls -l .locks/*.lock

# If >2 hours old, safe to remove
rm .locks/claude.lock
```

### Merge Conflict After Consolidation

```bash
# Review conflicts
git status

# Edit files to resolve

# Stage resolution
git add .

# Complete merge
git commit -m "resolve: merge conflicts from consolidation"
```

### Need to Undo Last Session

```bash
# Find recent backup
ls ~/backups/loralink-*

# Restore (WARNING: overwrites current work!)
cp -r ~/backups/loralink-20260309-1430/* ~/loralink/
```

---

## Tools Reference

| Script | Purpose | Location | Usage |
|---|---|---|---|
| `session-status.py` | Check repo status before work | `~/` | `python session-status.py` |
| `session-commit.py` | Save work safely at end of session | `~/` | `python session-commit.py` |
| `agent-tracking.py` | Manage locks and audit trail | Repo root | `python agent-tracking.py status` |
| `merge-to-github.py` | Consolidate on version change | Repo root | `python merge-to-github.py detect` |
| `AGENT_ASSIGNMENTS.md` | Component ownership | Repo root | Reference |

---

## File Locations Summary

```
~/
├── session-commit.py          ← Save work at session end
├── session-status.py          ← Check status before/after
├── SESSION_WORKFLOW.md        ← Full session guide
├── SESSION_CHECKLIST.md       ← Per-session template
├── QUICKSTART.md              ← 5-minute setup
│
├── loralink/                  ← LoRaLink workspace
│   ├── .locks/                ← Agent lock files (git-ignored)
│   ├── src/config.h           ← Firmware version (v0.0.1)
│   ├── agent-tracking.py      ← Lock management
│   ├── merge-to-github.py     ← Version consolidation
│   └── AGENT_ASSIGNMENTS.md   ← Component ownership
│
├── nutricalc/                 ← NutriCalc workspace
│   └── (same structure)
│
├── backups/                   ← Auto-created by session-commit.py
│   ├── loralink-20260309-1430/
│   ├── loralink-20260309-1500/
│   └── ...
│
└── logs/                      ← Auto-created by scripts
    ├── session-20260309-1430.log
    ├── agent-audit.log
    └── ...
```

---

## Quick Reference: Most Common Tasks

### "I want to work on BLEManager"
```bash
cd ~/loralink
python3 agent-tracking.py acquire Claude "Updating GATT response handling"
# ...edit and test...
python3 ~/session-commit.py
python3 agent-tracking.py release Claude
```

### "Check if anyone else is working"
```bash
cd ~/loralink
python3 agent-tracking.py status
```

### "I uploaded firmware, consolidate and push"
```bash
cd ~/loralink
python3 merge-to-github.py --auto-push
```

### "Show my modification history"
```bash
cd ~/loralink
python3 agent-tracking.py log
```

### "Restore from backup if I messed up"
```bash
ls ~/backups/
cp -r ~/backups/loralink-20260309-1430/* ~/loralink/
# WARNING: This overwrites current work
```

---

## Version Numbering Scheme

**Format:** `vMAJOR.MINOR.PATCH` (e.g., `v0.0.1`)

**When to increment:**
- **MAJOR:** Major breaking changes to protocol or architecture
- **MINOR:** New features, significant refactors
- **PATCH:** Bug fixes, small improvements (AUTO-INCREMENTED on firmware upload)

**Example progression:**
```
v0.0.1 (initial)
  ↓ (upload firmware)
v0.0.2 (auto-bumped patch)
  ↓ (add new feature, manual bump to minor)
v0.1.0 (new feature release)
  ↓ (upload)
v0.1.1 (auto-bumped)
```

---

## Summary: What's Different Now

| Before | Now |
|---|---|
| Multiple agents overwriting each other's work | Lock files prevent conflicts |
| No version tracking | Version in `src/config.h`, auto-increments on upload |
| Manual consolidation to GitHub | Automatic on version change via `merge-to-github.py` |
| No session workflow | `session-commit.py` handles safe storage |
| No audit trail | `agent-tracking.py` logs all modifications |
| GitHub is source of truth | PC is source of truth, GitHub is backup |
| Daily git chaos | Version-based consolidation (clean, intentional merges) |

---

## Next Steps (Immediate)

1. ✓ Create `.locks/` directory in both repos
2. ✓ Agents acquire locks before starting work
3. ✓ Use `agent-tracking.py status` to check conflict
4. ✓ Use `session-commit.py` at session end
5. ⚠️ **TODO:** Implement post-build hook in `platformio.ini` for auto-version-increment
6. ⚠️ **TODO:** Add discrete timestamp injection to web pages
7. ⚠️ **TODO:** Set up `.gitignore` to exclude `.locks/` and `~/backups/`

---

**System Created:** 2026-03-09
**Firmware Version:** v0.0.1
**Lock System:** Enabled
**Consolidation:** Ready (trigger on version change)
