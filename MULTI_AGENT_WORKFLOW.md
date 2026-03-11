# Multi-Agent Workflow — SDLC Guide

## Purpose

Defines the working process for multiple agents acting as a coordinated firmware team for LoRaLink.

This workflow is designed to support:

- Architecture planning
- Guarded implementation
- Integration review
- QA validation
- Versioned releases

Primary goal: **Ship stable firmware without letting multiple agents conflict, drift, or bypass review.**

---

## Operating Model

The workflow follows a linear stage-gate model:
**Orchestrator** ➔ **Architecture** ➔ **Implementation** ➔ **Integration** ➔ **QA** ➔ **Release**

Each stage has a distinct owner. No stage should be skipped for meaningful firmware changes.

---

## 🔄 The Continuous Product Cycle

To maintain momentum and quality, we operate in a rotating cycle across environments:

1.  **Discuss Requirements**: (Antigravity + Orchestrator)
    - Review goals, prioritize features, and define scope.
    - *Environment*: Antigravity.
2.  **Spec & Test Plan**: (Architect)
    - Write technical specs, command definitions, and verification criteria.
    - *Environment*: Claude Code / Claude Web.
3.  **Code & Unit Test**: (Developer)
    - Implement firmware/app logic on feature branches and verify builds.
    - *Environment*: Codex App / Codex Web.
4.  **Assure (QA)**: (QA Agent)
    - Execute test plan, perform broad analysis, and check for regressions.
    - *Environment*: Gemini Web Chat / ChatGPT Web Chat.
5.  **Integrate**: (Integrator)
    - Review code, verify tool/doc coupling, and prepare merge readiness.
    - *Environment*: Claude Code / Antigravity.
6.  **Release**: (Release Agent)
    - Consolidate stable work, update versions, and merge to `main`.
    - *Environment*: Antigravity.

---

## Environment Strategy

### Antigravity

Use for:

- Orchestration
- Architecture planning
- Broad QA analysis
- Release readiness coordination

### Claude Desktop

Use for:

- Code implementation
- Guarded refactors
- Integration review
- Scoped repository changes

This split keeps planning and coordination separate from code execution.

---

## Branch Strategy

### Stable Branch
- `main`

### Working Branch Types
- `feature/<topic>`
- `bugfix/<topic>`
- `refactor/<topic>`
- `release/<version>`

### Rules
- Never commit directly to `main`.
- Implementation always happens on a working branch.
- Release merges only happen after QA PASS.

---

## Agent Roles

### 1. Orchestrator Agent

**Purpose**: Coordinates the overall workflow.
**Environment**: Antigravity
**Assigned Agent**: Antigravity

### 2. Architecture Agent

**Purpose**: Plans changes before implementation begins.
**Environment**: Antigravity or Claude Desktop in read/analyze mode
**Assigned Agent**: Claude

### 3. Implementation Agent

**Purpose**: Develops and tests code changes.
**Environment**: Claude Code / Codex App
**Assigned Agent**: Codex

### 4. Integration Agent

**Purpose**: Reviews code and ensures system compatibility.
**Environment**: Claude Code / Antigravity
**Assigned Agent**: Claude

### 5. QA Agent

**Purpose**: Validates changes and checks for regressions.
**Environment**: Gemini Web Chat / ChatGPT Web Chat / Antigravity
**Assigned Agent**: Gemini / ChatGPT

### 6. Release Agent

**Purpose**: Manages releases and merges to `main`.
**Environment**: Antigravity
**Assigned Agent**: Antigravity

---

## Workflow Stages

### Stage 1: Intake
The user requests a change, fix, refactor, or release.
**Handled by**: Orchestrator Agent
**Outputs**: Task summary, priority, and assigned agent sequence.

---

### Stage 2: Architecture Review
The change is analyzed before coding begins.
**Handled by**: Architecture Agent
**Required outputs**:
- Affected files
- Manager interactions involved
- Tool coupling impacts
- Risk notes
- Recommended implementation phases

**Typical questions answered**:
- What part of the firmware is affected?
- What tools/docs must stay aligned?
- What validation is required?

---

### Stage 3: Branch Preparation
A working branch is selected or created.
**Handled by**: Implementation Agent
**Typical commands**:
```bash
git checkout main
git pull
git checkout -b feature/<topic>
```

---

### Stage 4: Implementation
Code and docs are updated.
**Handled by**: Implementation Agent
**Requirements**:
- Work only within task scope.
- Preserve architecture unless refactor is explicit.
- Update tools if firmware/API/command behavior changes.
- Update docs if commands/workflow/architecture changed.

**Required check before commit**: `pio run`

**Typical implementation cycle**:
```bash
git status
pio run
git add .
git commit -m "feat: describe change"
git push
```

---

### Stage 5: Integration Review
The working branch is reviewed for completeness and merge readiness.
**Handled by**: Integration Agent
**Checks**:
- Right files changed.
- No unrelated edits included.
- Docs updated where needed.
- Tools updated if coupling exists.
- Branch is mergeable with current `main`.

If issues are found: Return branch to Implementation stage.

---

### Stage 6: QA Validation
The change is validated.
**Handled by**: QA Agent
**Required checks**:
- Build passes (`pio run`).
- Affected managers behave as expected.
- Command routing still works.
- Transport and Scheduler behavior still work.
- Tools remain compatible.
- No obvious regressions.

**Result**: **PASS** or **FAIL**. If **FAIL**, list issues clearly and return to Implementation.

---

### Stage 7: Release Readiness
Approved work is prepared for stable merge.
**Handled by**: Release Agent
**Checks**:
- Integration review passed.
- QA passed.
- Version update considered.
- Release notes prepared.

---

### Stage 8: Merge / Release
Stable code is merged to `main`.
**Handled by**: Release Agent
**Typical commands**:
```bash
git checkout main
git pull
git merge feature/<topic>
git push
```

---

## Required Documentation Maintenance

Update the matching documents in the same branch where behavior changes:
- **Command changes**: Update `docs/COMMAND_INDEX.md`
- **Workflow changes**: Update `AGENT_ASSIGNMENTS.md` and `MULTI_AGENT_WORKFLOW.md`
- **Architecture changes**: Update `ARCHITECTURE_MAP.md`
- **Git/Process changes**: Update `docs/GIT_QUICK_REFERENCE.md`

---

## Tool Coupling Rule

Firmware and tools must stay synchronized. If firmware changes affect commands, API endpoints, scheduler behavior, BLE interfaces, or pin behavior, then you **MUST** review and update:
- `tools/ble_instrument.py`
- `tools/webapp/server.py`
- `tools/webapp/static/index.html`

---

## Commit Style

Recommended commit message format:
- `feat: ...`
- `fix: ...`
- `refactor: ...`
- `docs: ...`
- `release: ...`

---

## Parallel Agent Rule

Parallel work is allowed only when agents are isolated by branch or task scope.
- **Allowed**: Separate branches for separate features; read-only review in parallel with coding.
- **Not allowed**: Two implementation agents editing the same branch without coordination; QA validating code that is still changing.

---

## Minimal Daily Command Cadence

**Start of work**:
```bash
git checkout main
git pull
git checkout feature/<topic>
```

**During work**:
```bash
git status
pio run
git add .
git commit -m "type: summary"
git push
```

**Before merge**:
```bash
git checkout main
git pull
git merge feature/<topic>
```

---

## Release Standard

A version release includes:
- Merged approved branch.
- Passing build and QA.
- Updated version and documentation.
- Stable `main`.

---

## ⚠️ Workflow Fallback Strategy

In the event of model constraints, rate limits, or environment outages, follow these protocols:

1. **Environment Swap**:
   - If **Claude Code/Desktop** is unavailable ➔ Shift Implementation to **Antigravity**.
   - If **Codex App/Web** is unavailable ➔ Shift Development to **Claude Code**.
   - If **Gemini/ChatGPT Web** is unavailable ➔ Perform QA analysis in **Antigravity**.

2. **Context Pressure**:
   - If a task exceeds context limits ➔ **Orchestrator** must split the task into "Micro-Sprints" with their own Architecture and QA gates.

3. **Model Performance Degradation**:
   - If an agent consistently fails a stage (e.g., QA rejects Implementation 3+ times) ➔ **Orchestrator** must re-assign the role to a different model for a fresh review.

4. **Manual Override**:
   - If automation/locking scripts fail ➔ Revert to standard Git CLI. Clear all locks via `python agent-tracking.py --clear` before resuming.

---

## Final Principle

Agents are not free-roaming assistants. They are **role-bound team members** operating inside a controlled firmware workflow.
