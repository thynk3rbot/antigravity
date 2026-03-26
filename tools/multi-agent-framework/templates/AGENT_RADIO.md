# AGENT_RADIO — Three-Agent Coordination
**Status:** Active
**Team:** {{planner_name}} + {{executor_name}} + {{reviewer_name}}
**Project:** {{project_name}}

---

## Agentic Development Model

```
{{planner_name}} (Planning)        {{executor_name}} (Execution)       {{reviewer_name}} (Validation)
|- Plan & decide                |- Code generation              |- Test & validate
|- Architecture                 |- Search/replace               |- QA review
|- High-level strategy          |- Repetitive tasks             |- Report findings
'- Requirements                 '- Async processing             '- Release gating
```

**All three work in parallel. No blocking.**

---

## Daily Workflow (Locked Pattern)

### 09:00 PLAN
**{{planner_name}}:** What's blocking? What ships today?
**{{reviewer_name}}:** What validation is critical? What's the test plan?
**Local Model:** What async tasks can start?
**Action:** Queue async tasks immediately.

### 10:00 DECIDE
**{{planner_name}}:** Proposes architecture changes.
**{{reviewer_name}}:** Proposes tests and feedback.
**Both:** Agree on partition (who does what, what's off-limits).

### 11:00 IMPLEMENT
**{{executor_name}}:** Code features (parallel with validation).
**{{reviewer_name}}:** Test existing work (parallel with coding).
**Local Model:** Processing queue in background (no one waits).
**Key:** Three independent work streams. No blocking.

### 14:00 DEPLOY
**All:** Push code to main if builds clean.
**Local Model:** Results from queue are ready.
**Action:** Integrate local model outputs into codebase.

### 15:00 TEST
**{{executor_name}}:** Test against expectations.
**{{reviewer_name}}:** Validate integration.
**Both:** Document surprises and learnings.

### 17:00 RELEASE
**All:** If all tests pass -> tag version and ship.
**Action:** Queue next batch of async tasks for tomorrow.

---

## Local Model Integration

**Ollama (Local)** + **Cloud Fallback** via Hybrid Model Proxy.

- Prefer Local: Routes to Ollama ($0 cost) if healthy.
- Cloud Fallback: Routes to cloud provider if Ollama is down.
- Cost Tracking: Per-request token and USD metrics logging.
- RAG Augmentation: Domain knowledge injected automatically when enabled.

---

## Principles

1. **Ship working code** — working beats perfect
2. **Parallel work** — all agents independent
3. **Real feedback** — test on actual targets
4. **Async processing** — local model works 24/7, no blocking
5. **Incremental refinement** — refactor based on real evidence

---

## How to Use This Document

**For {{planner_name}}:**
1. Plan high-level strategy
2. Queue async tasks to local model
3. Continue architecture work
4. Check model results at DEPLOY phase

**For {{executor_name}}:**
1. Implement based on current phase spec
2. Run builds and tests
3. Log failures for review

**For {{reviewer_name}}:**
1. Validate work in parallel with implementation
2. Report findings and constraints
3. Feedback loops fast

**For Human:**
1. Monitor status
2. Review daily learnings
3. Make decisions on blockers
4. Ship the product
