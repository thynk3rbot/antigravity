# AI Agent Assignments & Roadmap Router

## 1. Global State Rules
All agents must read this file before executing tasks. The workspace operates on a strict **tollbooth state machine**. Do not modify files outside your designated phase directory or assigned scope.

* **Status Flags:** All `.md` files in planning, review, or status must include a frontmatter block:
  - `status`: [ backlog | planning | coding | testing | complete ]
  - `owner`: [ {{planner_name_lower}} | {{executor_name_lower}} | {{reviewer_name_lower}} | human ]

* **Branch Discipline:** No agent writes directly to `main`. All work happens on feature branches.

---

## 2. Directory Routing & Agent Scopes

| Phase | Directory | Primary Agent | Input | Output | Standards |
| --- | --- | --- | --- | --- | --- |
| **Phase 1: Planning** | `/{{phase_1_dir}}` | {{planner_name}} ({{planner_tool}}) | `{{phase_1_input}}` | `{{phase_1_output}}` | Strict specs, logic, dependencies. No code. |
| **Phase 2: Execution** | `/{{phase_2_dir}}` | {{executor_name}} | `{{phase_1_output}}` | Source files | Code generation, build verify, log errors. |
| **Phase 3: Review** | `/{{phase_3_dir}}` | {{reviewer_name}} | Source files | `{{phase_3_output}}` | QA pass, validation, tag complete. |

---

## 3. Detailed Agent Roles

### 1. Planning Agent ({{planner_name}})
**Purpose**: High-level architecture and requirement decomposition.
**Tool**: {{planner_tool}}
**Phase Ownership**: Phase 1 (`/{{phase_1_dir}}`)

### 2. Execution Agent ({{executor_name}})
**Purpose**: Implementation and local compilation.
**Tool**: {{executor_tool}}
**Phase Ownership**: Phase 2 (`/{{phase_2_dir}}`)

### 3. Review Agent ({{reviewer_name}})
**Purpose**: Final QA, validation, and release gatekeeping.
**Tool**: {{reviewer_tool}}
**Phase Ownership**: Phase 3 (`/{{phase_3_dir}}`)

---

## 4. Resource Ownership (Lock System)

The `.locks/` system is the source of truth for concurrency prevention.

| Agent | Lock File | Directories |
| --- | --- | --- |
| **{{planner_name}}** | `.locks/{{planner_lock}}` | {{planner_dirs}} |
| **{{executor_name}}** | `.locks/{{executor_lock}}` | {{executor_dirs}} |
| **{{reviewer_name}}** | `.locks/{{reviewer_lock}}` | {{reviewer_dirs}} |

---

## 5. Workflow Fallback & Emergency
If an agent hits rate limits or environment failure:
1. Human intervention clears stuck locks via `python agent_tracking.py clear`.
2. Rollback to last known good state if a phase fails repeatedly.

---

**Release Principle:** Only work tagged `status: complete` in `/{{phase_3_dir}}` is eligible for merge to `main`.
