# 🛡️ LoRaLink Joint Release Strategy

This document defines the **Ironclad Operational Protocol** for coordinated development between Antigravity (Gemini) and Claude.

## 1. Agent Responsibilities
| Agent | Role | Primary Tasks |
| :--- | :--- | :--- |
| **Antigravity (Gemini)** | **Orchestrator / Gatekeeper** | Planning, Logic Audits, Build Verification, Git Management, User Notification. |
| **Claude** | **Execution / Functional Coding** | Implementation, Cleanup, Tooling, WebApp Stability, Branch Maintenance. |

## 2. The "Tollbooth" Workflow
All changes must pass through a 3-Phase verification cycle before merging to `main`:
1.  **Phase 1 (Plan)**: Antigravity drafts the technical spec in `/01_planning/`. Reviewed by Claude.
2.  **Phase 2 (Execute)**: Claude implements logic on the `feature/v2-rationalization` branch.
3.  **Phase 3 (Verify)**: Antigravity runs the **Full Build Matrix** (V2, V3, V4) and performs a code audit.

## 3. Mandatory Build Gate
**NO MERGE TO MAIN** unless the following command passes with 0 errors:
```powershell
python -m platformio run -e heltec_v2 && python -m platformio run -e heltec_v3 && python -m platformio run -e heltec_v4
```
*Responsibility: Antigravity must execute and verify this after every functional commit.*

## 4. Documentation Standards
- **Coordination**: Use `docs/AGENT_RADIO.md` for high-frequency technical talk.
- **Mission Board**: Use `docs/AGENT_COMMAND_CENTER.md` for high-level phase tracking and blockers.
- **Stability**: Lessons learned must be persisted via the `/persist-lesson` workflow into `STABILITY_MANIFEST.md`.

## 5. Branching & Git Hygiene
- **Baseline**: `main` remains the protected "world-class" production code.
- **Active Dev**: All rationalization happens on `feature/v2-rationalization`.
- **Atomic Commits**: Every commit must represent a single Phase or sub-Phase (e.g., "Phase 1: Dead Code removal").

---
**Signed by Agents:**
- [x] Antigravity (Gemini) - 2026-03-24
- [x] Claude - 2026-03-24
