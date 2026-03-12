# AI Agent Assignments & Roadmap Router

## 1. Global State Rules
All agents must read this file before executing tasks. The workspace operates on a strict **tollbooth state machine**. Do not modify files outside your designated phase directory or assigned scope.

* **Status Flags:** All `.md` files in planning, review, or status must include a frontmatter block:
  - `status`: [ backlog | planning | coding | testing | complete ]
  - `owner`: [ openai | claude | antigravity | human ]

* **Branch Discipline:** No agent writes directly to `main`. All work happens on feature branches.

---

## 2. Directory Routing & Agent Scopes

| Phase | Directory | Primary Agent | Input | Output | Standards |
| --- | --- | --- | --- | --- | --- |
| **Phase 1: Planning** | `/01_planning/` | OpenAI (sgpt) | `idea.txt` | `spec.md` | Strict tech specs, pinouts, logic. No code. |
| **Phase 2: Execution** | `/02_coding/` | Claude Code | `spec.md` | Source files | Code generation, build verify, log errors. |
| **Phase 3: Review** | `/03_review/` | Antigravity | Source files | `audit_report.md`| Browser/test agents, QA pass, tag complete. |

---

## 3. Detailed Agent Roles

### 1. Planning Agent (OpenAI / sgpt)
**Purpose**: High-level architecture and requirement decomposition.
**Environment**: Terminal (sgpt)
**Phase Ownership**: Phase 1 (`/01_planning/`)
**Responsibilities**:
- Read `idea.txt` or user prompts.
- Generate `spec.md` in `/01_planning/`.
- Define exact pinouts (ESP32-S3), manager interactions, and logic flow.

### 2. Execution Agent (Claude Code)
**Purpose**: Implementation and local compilation.
**Environment**: Claude Desktop / CLI
**Phase Ownership**: Phase 2 (`/02_coding/`)
**Responsibilities**:
- Implement code based on Phase 1 spec.
- Write source files to `/02_coding/` (or designated `src/` dirs if in-place).
- Run `pio run` to verify build.
- Log failures to `/02_coding/error.log`.
- Halt after 3 failed retries.

### 3. Review & Orchestration Agent (Antigravity)
**Purpose**: Final QA, browser-based validation, and release gatekeeping.
**Environment**: Antigravity Workspace
**Phase Ownership**: Phase 3 (`/03_review/`)
**Responsibilities**:
- Review Phase 2 output.
- Perform visual QA via browser sub-agents.
- Execute regression tests.
- Generate `audit_report.md` in `/03_review/`.
- Transition status to `complete`.

---

## 4. Resource Ownership (Lock System)

The `.locks/` system remains the source of truth for concurrency prevention.

| Agent | Lock File | Root Directories / Scopes |
| --- | --- | --- |
| **OpenAI** | `.locks/openai.lock` | `/01_planning/`, `docs/plans/` |
| **Claude** | `.locks/claude.lock` | `/02_coding/`, `src/`, `tools/webapp/` |
| **Antigravity** | `.locks/antigravity.lock` | `/03_review/`, `docs/`, `main.cpp` |

---

## 5. Workflow Fallback & Emergency
If an agent hits rate limits or environment failure:
1. **Antigravity** assumes Orchestrator role to re-route tasks.
2. **Human** intervention kills stuck processes via `python agent-tracking.py clear`.
3. **Rollback** to last known good state if Phase 2 fails 3x.

---

**Release Principle:** Only work tagged `status: complete` by Antigravity in `/03_review/` is eligible for merge to `main`.
