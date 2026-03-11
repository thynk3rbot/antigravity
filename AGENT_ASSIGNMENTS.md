# Agent Assignments & Roles

## Purpose

Defines the agent roles for LoRaLink development and release work. This project uses a multi-agent workflow where agents act like team members with clear boundaries.

Core rules:
- No agent writes directly to `main`.
- All implementation happens on feature branches.
- Release flow is planned, reviewed, tested, and then merged.

---

## Primary Environments

### Antigravity
Use for:
- Orchestration
- Architecture planning
- Cross-agent coordination
- Broad QA analysis
- Release readiness review

### Claude Desktop
Use for:
- Guarded code implementation
- Branch-scoped edits
- Reviewable refactors
- Policy-respecting code changes
- Merge prep

---

## Active Agent Assignments

| Role | Agent | Primary Environments |
| --- | --- | --- |
| **Orchestrator** | Antigravity | Antigravity |
| **Architect** | Claude | Claude Code / Claude Web |
| **Developer** | Codex | Codex App / Codex Web |
| **Integrator** | Claude | Claude Code / Antigravity |
| **QA** | Gemini / ChatGPT | Gemini Web Chat / ChatGPT Web Chat |
| **Release** | chatbot | Antigravity |

---

## Component Ownership (Resource-Based)

To prevent concurrent editing conflicts on the source of truth, primary file ownership remains:

| Agent/Environment | Primary Focus | Lock File | Root Directories |
| --- | --- | --- | --- |
| **Antigravity** | LoRaLink firmware core, managers, radio stack | `.locks/antigravity.lock` | `src/managers/`, `src/config.h`, `src/crypto.h`, `src/main.cpp` |
| **Claude Desktop** | PC and Web applications, code reviews, integration tests | `.locks/claude.lock` | `tools/webapp/`, `tools/pc_app/`, `docs/`, `INTEGRATION.md` |
| **Codex** | Firmware optimizations, performance tuning, watchdog | `.locks/codex.lock` | `src/managers/PerformanceManager.*`, `src/managers/PowerManager.*` |

---

## Agent Roles

### 1. Orchestrator Agent
**Purpose**: Coordinates the overall workflow.
**Environment**: Antigravity
**Assigned Agent**: Antigravity
**Responsibilities**:
- Receives user goals
- Selects which agent acts next
- Maintains task order
- Prevents overlapping work
- Tracks release readiness
- Ensures branch discipline is followed

**Can**:
- Assign work
- Request reviews
- Request QA passes
- Request release prep

**Cannot**:
- Directly merge to `main`
- Bypass QA or release checks

### 2. Architecture Agent
**Purpose**: Plans changes before implementation begins.
**Environment**: Antigravity or Claude Desktop in read/analyze mode
**Assigned Agent**: Claude
**Responsibilities**:
- Inspect repository structure
- Understand current manager interactions
- Identify affected files
- Identify tool coupling impacts
- Propose implementation phases
- Identify risks and validation needs

**Outputs**:
- Implementation plan
- Affected files list
- Risk notes
- Commit grouping suggestion

**Cannot**:
- Start coding before analysis is complete

### 3. Implementation Agent
**Purpose**: Makes code changes on a feature branch.
**Environment**: Claude Desktop
**Assigned Agent**: Codex (Developer)
**Responsibilities**:
- Create or use assigned feature branch
- Modify only the files needed
- Preserve existing architecture unless refactor is intentional
- Update coupled tooling when firmware behavior changes
- Update docs when commands, workflow, or architecture change

**Required checks before commit**:
- Firmware builds (`pio run`)
- Changed files are scoped to the task
- Docs/tool coupling reviewed

**Can**:
- Edit source
- Edit docs
- Update tools

**Cannot**:
- Commit to `main`
- Merge its own work into `main`
- Skip build verification

### 4. Integration Agent
**Purpose**: Prepares completed work for merge.
**Environment**: Antigravity or Claude Desktop
**Assigned Agent**: Claude
**Responsibilities**:
- Inspect diffs from feature branch
- Verify intended files changed
- Detect missing tool/doc updates
- Resolve merge conflicts if needed
- Ensure branch is ready for QA/release

**Checks**:
- Command changes reflected in docs
- Tool changes applied if firmware/API/commands changed
- No accidental unrelated edits
- Branch merges cleanly with current `main`

**Cannot**:
- Approve broken builds
- Skip required coupling updates

### 5. QA Agent
**Purpose**: Validates functionality and regression risk.
**Environment**: Antigravity preferred for broad analysis
**Assigned Agent**: Gemini
**Responsibilities**:
- Verify build success
- Inspect core behavior affected by the change
- Check routing, scheduler, transport, and tool compatibility as relevant
- Identify regressions
- Produce **PASS** / **FAIL** result

**Typical checks**:
- `pio run`
- Command routing behavior
- LoRa behavior
- BLE behavior if touched
- WiFi/API behavior if touched
- Scheduler stability if touched
- Docs/tool sync if command or API behavior changed

**Output**:
- **PASS** or **FAIL** with bug list

### 6. Release Agent
**Purpose**: Handles stable merge and version release steps.
**Environment**: Antigravity for coordination
**Assigned Agent**: lightweight chatbot
**Responsibilities**:
- Confirm QA pass
- Verify release checklist
- Update version if appropriate
- Merge approved work to `main`
- Create release notes
- Tag release if used in workflow

**Cannot**:
- Release unreviewed work
- Bypass QA
- Ignore versioning rules

---

## Repo Rules for All Agents

- Never commit directly to `main`.
- Always work on `feature/<topic>`, `bugfix/<topic>`, `refactor/<topic>`, or `release/<version>`.
- Always review tool coupling.
- Always preserve firmware/tool compatibility.
- Always build (`pio run`) before commit when code changed.

---

## Documentation Rules

Agents must update relevant docs when behavior changes.

### If commands change
Update: `docs/COMMAND_INDEX.md`

### If workflow changes
Update: `AGENT_ASSIGNMENTS.md` and `MULTI_AGENT_WORKFLOW.md`

### If architecture changes materially
Update: `ARCHITECTURE_MAP.md`

### If firmware behavior affects tools
Review/update:
- `tools/ble_instrument.py`
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`

---

## Branch Ownership Rule

One implementation agent owns one working branch at a time. If parallel work is needed:
- Use separate branches.
- Merge only after integration review.
- Avoid two agents editing the same feature branch simultaneously.

---

## ⚠️ Fallback Strategy

If a model or environment becomes unavailable (rate limits, downtime, context limits):
- **Redundancy**: Orchestrator re-assigns the task to the next available environment (e.g., Claude ➔ Antigravity).
- **Decomposition**: Large tasks are split into smaller sub-tasks to fit context windows.
- **Manual Intervention**: The user may act as a human agent to clear blockers or perform manual integration.

---

## Release Principle

Stable firmware lives on `main`. Everything else is staging, feature work, or validation.
