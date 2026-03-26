# Multi-Agent Workflow — SDLC Guide

## 1. The 3-Phase Tollbooth Model

This workflow enforces a linear progression from idea to release through three isolated directories and agent handoffs.

### Phase 1: Planning (/{{phase_1_dir}})
*   **Agent**: {{planner_name}} ({{planner_tool}})
*   **Goal**: Architecture and Requirements
*   **Handoff**: `{{phase_1_output}}` with `status: planning`
*   **Rule**: No source code is written here. Only logic, dependencies, and architecture.

### Phase 2: Execution (/{{phase_2_dir}})
*   **Agent**: {{executor_name}}
*   **Goal**: Implementation and Build Verification
*   **Handoff**: Working code with `status: coding`
*   **Rule**: Must follow `{{phase_1_output}}`. {{build_verify_rule}}Log errors to `error.log`.

### Phase 3: Review (/{{phase_3_dir}})
*   **Agent**: {{reviewer_name}}
*   **Goal**: QA, Validation, and Integration
*   **Handoff**: `{{phase_3_output}}` with `status: complete`
*   **Rule**: Verify logic and integration. Tag as complete when passing.

---

## 2. Directory Routing Rules

1.  **Isolation**: Do not perform Phase 2 work in `/{{phase_1_dir}}`.
2.  **State Machine**: Files move from phase to phase via user or orchestrator commands.
3.  **Frontmatter Requirement**:
    ```markdown
    ---
    status: planning
    owner: {{planner_name_lower}}
    ---
    ```

---

## 3. Daily Developer Cadence

1.  **Acquire Lock**: `python agent_tracking.py acquire <AgentName> "Task description"`
2.  **Sync**: `git pull origin feature/topic`
3.  **Implement**: Work according to current phase spec.
4.  **Verify**: Run build/test commands. Log failures if any.
5.  **Commit**: `git add . && git commit -m "feat: phase N implementation"`
6.  **Release Lock**: `python agent_tracking.py release <AgentName>`

---

## 4. Branch Strategy

- `main`: **STABLE ONLY**. No direct edits.
- `feature/*`: Development and phase progression.
- `release/*`: Stabilized code ready for main merge.

---

## 5. Emergency Recovery

- **Workflow Stalls**: Use `python agent_tracking.py clear` to reset locks if an agent crashes.
- **Rollback**: Revert to last known good state if Phase 2 fails repeatedly.

---

**Final Principle:** The workspace is a factory line. Stability is built into the routing.
