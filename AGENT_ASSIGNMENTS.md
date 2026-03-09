# Agent Assignments & Lock File System

**Purpose:** Prevent multiple AI agents from overwriting each other's work on shared components. Uses file-based locking and version-based merging.

---

## Core Principle

1. **PC is Source of Truth** — All work happens locally; GitHub is backup/archive
2. **Lock Files Prevent Conflicts** — Agent acquires lock before modifying assigned files
3. **Version-Based Consolidation** — Merge all agent work to GitHub when version increments
4. **Manual Merge on Version Change** — When `.h` file version changes, consolidate and push

---

## Current Component Assignments

| Agent | Primary Focus | Lock File | Root Directories |
|---|---|---|---|
| **Claude** | LoRaLink firmware core, managers, command routing | `.locks/claude.lock` | `src/managers/`, `src/config.h`, `src/crypto.h` |
| **Antigravity** | NutriCalc server, MQTT integration, solver algorithm | `.locks/antigravity.lock` | `tools/webapp/`, `tools/requirements.txt` |
| **Codex** | Firmware optimizations, performance tuning, watchdog | `.locks/codex.lock` | `src/main.cpp`, `src/managers/PerformanceManager.*` |

**Secondary (shared read-only unless assigned):**
- `README.md`, `CLAUDE.md`, `INTEGRATION.md` — shared documentation
- `tools/ble_instrument.py` — anyone can use, only assigned agent modifies
- GitHub workflows, CI/CD configuration — centralized

---

## Lock File Structure

Location: `.locks/` directory (git-ignored)

Each lock file contains:
```
agent_name
timestamp_acquired
session_start_time
task_description
```

**Example:** `.locks/claude.lock`
```
Claude
2026-03-09T14:30:00Z
session-20260309-1430
"Refactoring BLEManager GATT response callback"
```

---

## How Agents Use Locks

### Before Working on Assigned Component

```bash
# 1. Check if lock already exists and is fresh
if [ -f ".locks/claude.lock" ]; then
    # If older than 2 hours, safe to overwrite (agent abandoned it)
    # If recent, wait or coordinate with other agent
fi

# 2. Create lock file
cat > .locks/claude.lock << EOF
Claude
$(date -u +%Y-%m-%dT%H:%M:%SZ)
session-$(date +%Y%m%d-%H%M)
"Brief description of work"
EOF

# 3. Do work on assigned files

# 4. Run session-commit.py to save changes
python3 ~/session-commit.py

# 5. Remove lock when done (optional but clean)
rm .locks/claude.lock
```

### Lock Timeout

If a lock file is **>2 hours old**, it's considered abandoned and can be safely removed:

```bash
# Check lock age
find .locks -name "*.lock" -mmin +120 -delete
```

---

## Merge-to-GitHub Workflow

### When Version Changes (Triggers Consolidation)

**File:** `src/config.h`
```c
#define FIRMWARE_VERSION "v0.0.2"  // Changed from v0.0.1
```

**Automatic Actions:**
1. `merge-to-github.py` detects version change
2. Consolidates all agent lock files into session summary
3. Stages all changes: `git add -A`
4. Creates consolidation commit with all agent work
5. Pushes to `main` (or current branch)

**Example Consolidation Commit:**
```
consolidate: v0.0.1 → v0.0.2 (all agents)

Claude: Updated BLEManager GATT callbacks (src/managers/BLEManager.cpp)
Antigravity: Improved MQTT payload validation (tools/webapp/server.py)
Codex: Optimized ScheduleManager task queue (src/managers/ScheduleManager.cpp)

Lock files cleared. Session summary: See session-20260309-1430.log
```

---

## File Modification Rules

### If Your Agent is NOT Assigned to Component

- **Read:** ✓ Yes (review code, understand architecture)
- **Modify:** ✗ No (must coordinate with assigned agent)
- **Comment:** ✓ Yes (add inline TODO, request in issue)

### If Your Agent IS Assigned

- **Acquire Lock:** Required (protects from concurrent edits)
- **Modify:** Full authority over assigned files
- **Commit:** Via `session-commit.py` (not direct git)
- **Push:** Only on version change or explicit user request

---

## Preventing Conflicts: Checklist

- [ ] Check `.locks/` before starting work
- [ ] Create lock file with timestamp and description
- [ ] Only modify files in your assigned directories
- [ ] Run `session-status.py` to verify no other agent is modifying
- [ ] Use `session-commit.py` to save (creates backup to `~/backups/`)
- [ ] Remove lock file when done
- [ ] Coordinate with other agents if overlapping work needed

---

## Quick Reference: Component Ownership

### Claude Owns (Firmware Core)

```
src/managers/BLEManager.cpp
src/managers/BLEManager.h
src/managers/CommandManager.cpp
src/managers/CommandManager.h
src/managers/LoRaManager.cpp
src/managers/LoRaManager.h
src/managers/ScheduleManager.cpp
src/managers/ScheduleManager.h
src/managers/WiFiManager.cpp
src/managers/WiFiManager.h
src/config.h
src/crypto.h
```

### Antigravity Owns (NutriCalc & Integrations)

```
tools/webapp/server.py
tools/webapp/static/
tools/requirements.txt
tools/webapp/configs/
INTEGRATION.md (co-authored)
```

### Codex Owns (Performance & Optimization)

```
src/main.cpp
src/managers/PerformanceManager.cpp
src/managers/PerformanceManager.h
src/managers/PowerManager.cpp
src/managers/PowerManager.h
```

---

## Lock File Troubleshooting

**Q: Lock file stuck from interrupted session?**
```bash
# Check age
ls -l .locks/*.lock
# If >2 hours, safe to remove
rm .locks/agent_name.lock
```

**Q: Merge failed because files were edited by multiple agents?**
```bash
# Review git diff to find conflicting edits
git diff --check
# If conflicts minor, use git merge conflict resolution
# If conflicts major, coordinate with other agents and re-do
```

**Q: How do I know if another agent is working?**
```bash
# Check all active locks
cat .locks/*.lock 2>/dev/null

# Check modified files since lock was created
git status --short
```

---

## Session Summary Format

After each session ends (via `session-commit.py`), a summary appears:

```
============================================================
SESSION COMPLETE
============================================================
Agent: Claude
Duration: 1h 22m
Files changed: 8
Commits created: 1
Lock file: .locks/claude.lock (removed)
Backup location: ~/backups/loralink-20260309-1430
GitHub status: Not pushed (version unchanged)
============================================================
```

---

## Version Auto-Increment on Firmware Upload

When `pio run -t upload` or OTA deployment occurs, a post-build hook should:

1. Read current `FIRMWARE_VERSION` from `src/config.h`
2. Parse semantic version (e.g., `v0.0.1` → `[0, 0, 1]`)
3. Increment patch version: `[0, 0, 2]`
4. Write back: `#define FIRMWARE_VERSION "v0.0.2"`
5. Auto-stage and commit: `"fw: auto-bump v0.0.1 → v0.0.2"`

**See:** `platformio.ini` post-build hook configuration (TODO: add exact snippet when implementing)

---

## Next Steps

1. Create `.locks/` directory (git-ignored)
2. Each agent creates initial lock file before starting work
3. Run `agent-tracking.py` to log which files each agent modifies
4. On version change, run `merge-to-github.py` to consolidate
5. Clear locks after merge, start fresh on new feature branch

---

**Last Updated:** 2026-03-09
**Version:** 1.0 (Lock System v0.0.1)
