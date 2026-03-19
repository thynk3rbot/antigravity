# Repository Unification — "One Branch, One Truth"

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Merge all scattered branch work into `main`, delete stale branches, and codify workflow rules so both Claude sessions always work from the same codebase.

**Architecture:** Fast-forward `main` to `origin/main`, merge `feature/v2-firmware` into `main` via PR, cherry-pick 3 small items from `spw1`, then delete all stale local+remote branches. Add workflow rules to `CLAUDE.md` to prevent future sprawl.

**Tech Stack:** Git, GitHub CLI (`gh`)

**Pre-conditions verified by diff analysis (2026-03-19):**
- `feature/lora-traffic-optimization` → 100% superseded by current v1
- `feature/auto-discovery-optimization` → 100% superseded by current v1
- `feature/topology-map-and-group-broadcast` → already merged (HEAD is descendant)
- `spw1` → 3 minor items to cherry-pick (WEBAPP.md cleanup, native test env, test skeleton)
- `feature/pc-tools-and-contribution-hooks` → pushed but empty locally
- `feature/webapp-ux-and-event-propagation` → already merged to main via PR #2

---

### Task 1: Update local `main` to match remote

**Files:** None (git-only)

**Step 1: Fetch and fast-forward main**

```bash
git fetch origin
git checkout main
git merge --ff-only origin/main
```

Expected: `main` moves from `d2b26f9` to `6c9dab4` (picks up 20 commits from remote).

**Step 2: Verify main is current**

```bash
git log --oneline -3
```

Expected: Top commit is `6c9dab4 feat(release): Consolidate all features and stability fixes for v0.0.7`

**Step 3: Return to feature branch**

```bash
git checkout feature/v2-firmware
```

---

### Task 2: Cherry-pick useful items from `spw1` into `feature/v2-firmware`

**Files:**
- Modify: `WEBAPP.md` (replace raw Gemini transcript with clean spec)
- Modify: `platformio.ini` (root-level, add native test env — NOTE: only if root platformio.ini exists, else skip)
- Create: `test/test_native/test_main.cpp` (Unity test skeleton)

**Step 1: Cherry-pick the 3 spw1 commits**

These are squashed into a single manual commit because the spw1 commits also touch files we don't want (increment_version.py, RELEASE_NOTES.md, SYSTEM.md).

Instead, manually apply only what we want:

```bash
# Check if root platformio.ini exists
ls platformio.ini 2>/dev/null
```

If no root `platformio.ini`, skip the native test env (it belongs to the old flat layout). The v1 and v2 each have their own `platformio.ini` under `firmware/`.

**Step 2: Clean up WEBAPP.md**

Replace the raw Gemini conversation transcript with a clean Fleet Administrator spec. Use the content from `spw1` branch as reference:

```bash
git show spw1:WEBAPP.md > /tmp/webapp_spw1.md
# Review it, then decide: if WEBAPP.md on HEAD is still the raw transcript, replace it
```

If current WEBAPP.md is still the Gemini transcript → replace with cleaned version.
If it's already been updated → skip.

**Step 3: Commit cherry-picked items**

```bash
git add -A
git commit -m "chore: cherry-pick WEBAPP.md cleanup from spw1 branch"
```

---

### Task 3: Push feature branch and create PR to main

**Files:** None (git-only)

**Step 1: Push feature/v2-firmware to remote**

```bash
git push origin feature/v2-firmware
```

**Step 2: Create PR**

```bash
gh pr create \
  --base main \
  --head feature/v2-firmware \
  --title "unify: merge all v2 firmware, v1 fixes, and tools into main" \
  --body "$(cat <<'EOF'
## Summary
- Merges the full v2 firmware test bed (`firmware/v2/`)
- Brings v1 firmware to v0.2.82 with GPS V4 support, Web-OTA, power management
- Adds version management tooling (`tools/version.sh`, `.version`)
- Adds ScheduleManager, unified CommandManager routing, fleet build environments
- Cherry-picks WEBAPP.md cleanup from spw1

## Context
All other feature branches (`lora-traffic-optimization`, `auto-discovery-optimization`,
`topology-map-and-group-broadcast`, `spw1`) have been verified as fully superseded
by this branch. Diff analysis performed 2026-03-19 confirms zero missing features.

## After merge
- Delete all stale feature branches (local + remote)
- Set origin/HEAD to main
- Codify "one branch, one truth" workflow in CLAUDE.md

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

**Step 3: Merge the PR**

```bash
gh pr merge --merge --delete-branch
```

Note: `--delete-branch` only deletes the `feature/v2-firmware` remote branch. We handle other branches in Task 4.

---

### Task 4: Delete all stale branches (local + remote)

**Files:** None (git-only)

**Step 1: Switch to main and pull the merge**

```bash
git checkout main
git pull origin main
```

**Step 2: Delete stale LOCAL branches**

```bash
git branch -D feature/lora-traffic-optimization
git branch -D feature/auto-discovery-optimization
git branch -D feature/topology-map-and-group-broadcast
git branch -D feature/pc-tools-and-contribution-hooks
git branch -D spw1
git branch -D feature/v2-firmware 2>/dev/null  # may already be gone from PR merge
```

**Step 3: Delete stale REMOTE branches**

```bash
git push origin --delete feature/lora-traffic-optimization
git push origin --delete feature/auto-discovery-optimization
git push origin --delete feature/topology-map-and-group-broadcast
git push origin --delete feature/pc-tools-and-contribution-hooks
git push origin --delete feature/webapp-ux-and-event-propagation
git push origin --delete spw1
```

**Step 4: Set remote HEAD to main**

```bash
gh api repos/thynk3rbot/antigravity -X PATCH -f default_branch=main
```

**Step 5: Prune and verify**

```bash
git fetch --prune
git branch -a
```

Expected: Only `main` locally, only `origin/main` remotely.

---

### Task 5: Codify workflow rules in CLAUDE.md

**Files:**
- Modify: `CLAUDE.md` — add "Unified Workflow" section

**Step 1: Add workflow rules**

Add the following section to `CLAUDE.md` after the Git Workflow section:

```markdown
## Unified Workflow — One Branch, One Truth [CRITICAL]

**All Claude sessions and all IDEs work from `main`.** This is non-negotiable.

### Rules
1. **`main` is the single source of truth.** Both firmware versions, all tools, all docs live here.
2. **Feature branches are short-lived.** Branch from `main`, do the work, PR back within the same session or next day.
3. **Never accumulate parallel long-lived branches.** If a feature branch is >2 days old without a PR, something is wrong.
4. **Flash from `main` only.** All PlatformIO build environments target the `main` branch.
5. **Both `firmware/v1/` and `firmware/v2/` coexist.** v1 is active development, v2 is test bed.

### Directory Layout (canonical)
```
main/
├── firmware/v1/     ← Active development firmware (flash this)
├── firmware/v2/     ← V2 test bed
├── tools/           ← Webapp, version scripts, fleet tools
├── docs/            ← Plans, versioning, specs
├── .version         ← Version state file
└── CLAUDE.md        ← This file
```
```

**Step 2: Commit workflow rules**

```bash
git add CLAUDE.md
git commit -m "docs: codify unified workflow rules in CLAUDE.md

Establishes 'one branch, one truth' policy after repo unification.
All Claude sessions and IDEs work from main. Feature branches are
short-lived and merge back within same session or next day."
```

---

### Task 6: Verify clean state

**Step 1: Verify branch state**

```bash
git branch -a
```

Expected: Only `* main` locally, `remotes/origin/main` remotely.

**Step 2: Verify v1 builds**

```bash
cd firmware/v1 && pio run -e heltec_wifi_lora_32_V3 2>&1 | tail -5
```

Expected: `SUCCESS` (or at minimum, compiles without error).

**Step 3: Verify v2 builds**

```bash
cd firmware/v2 && pio run -e heltec_v3_hub 2>&1 | tail -5
```

Expected: `SUCCESS`

**Step 4: Final commit if any cleanup needed**

```bash
git status
```

Expected: Clean working tree on `main`.
