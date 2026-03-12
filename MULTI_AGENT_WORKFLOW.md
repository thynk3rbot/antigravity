# Multi-Agent Workflow - SDLC Guide

## 1. The 3-Phase Tollbooth Model

This workflow enforces a linear progression from idea to release through three isolated directories and agent handoffs.

### Phase 1: Planning (/01_planning/)
*   **Agent**: OpenAI (sgpt)
*   **Goal**: Architecture and Requirements
*   **Handoff**: `spec.md` with `status: planning`
*   **Rule**: No source code is written here. Only logic, pinouts, and dependencies.

### Phase 2: Execution (/02_coding/)
*   **Agent**: Claude Code
*   **Goal**: Implementation and Build Verification
*   **Handoff**: Working code with `status: coding`
*   **Rule**: Must follow `spec.md`. Must pass `pio run`. Log errors to `error.log`.

### Phase 3: Review (/03_review/)
*   **Agent**: Antigravity
*   **Goal**: QA, Validation, and Integration
*   **Handoff**: `audit_report.md` with `status: complete`
*   **Rule**: Verify logic and UI. Trigger `merge-to-github.py` for consolidation.

---

## 2. Directory Routing Rules

1.  **Isolation**: Do not perform Phase 2 work in `/01_planning/`.
2.  **State Machine**: Files move from phase to phase via user or orchestrator commands.
3.  **Frontmatter Requirement**:
    ```markdown
    ---
    status: planning
    owner: openai
    ---
    ```

---

## 3. Daily Developer Cadence (Claude Code)

1.  **Acquire Lock**: `python agent-tracking.py acquire Claude "Implementing Phase 1 Spec"`
2.  **Sync**: `git pull origin feature/topic`
3.  **Implement**: Write files according to `/01_planning/spec.md`.
4.  **Verify**: `pio run`. If failed, log to `/02_coding/error.log`.
5.  **Commit**: `git add . && git commit -m "feat: phase 2 implementation"`
6.  **Release Lock**: `python agent-tracking.py release Claude`

---

## 4. Orchestrator Cadence (Antigravity)

1.  **Review Phase 2**: Branch check and logic audit.
2.  **QA**: Use browser sub-agents to verify `cockpit.html` and telemetry.
3.  **Audit**: Generate `/03_review/audit_report.md`.
4.  **Consolidate**: Run `python merge-to-github.py consolidate` to prepare for release.
5.  **Tag**: Mark work as `complete`.

---

## 5. Branch Strategy

- `main`: **STABLE ONLY**. No direct edits.
- `feature/*`: Development and phase progression.
- `release/*`: Stabilized code ready for main merge.

---

## 6. Emergency Recovery

- **Port Locks**: Kill Python server or unplug ESP32 if COM port busy.
- **Workflow Stalls**: Use `agent-tracking.py --clear` to reset the state machine if an agent crashes while holding a lock.
- **Auto-Increment**: The `merge-to-github.py --auto-increment` script is the primary way to bump fleet versions baseline.

---

**Final Principle:** The workspace is a factory line. Stability is built into the routing.
