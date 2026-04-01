# Quality Pipeline — AG Implementation Prompt

**Date:** 2026-04-01
**Prerequisites:** `git pull origin main`
**Goal:** Build the recurring code quality pipeline that uses Ollama to review, grade, learn, and teach — feeding institutional knowledge into RAG.

---

## PROMPT START

You are building a **code quality pipeline** that runs Ollama against the codebase to produce graded reviews, extract patterns, and generate lessons. This is a recurring tool — it runs daily (cron or manually) and its output feeds into the RAG knowledge base so that all agents (Claude, AG, Ollama) share institutional memory.

### Existing Code to Reference (READ FIRST)

1. **`tools/multi-agent-framework/ollama_bridge.py`** — The current Ollama CLI wrapper. Simple synchronous HTTP calls to `/api/generate`. Use this as the foundation for your Ollama client — extend it, don't rewrite it.
2. **`tools/multi-agent-framework/rag/ingest.py`** — ChromaDB document ingester. Chunks files and stores embeddings. You'll use this to push pipeline output into RAG.
3. **`tools/multi-agent-framework/rag/retriever.py`** — ChromaDB query interface. Pipeline consumers use this.
4. **`docs/plans/2026-04-01-dashboard-and-quality-pipeline-design.md`** — The full design doc. Sections 7-12 are your spec. Read all of Part 2.

### What You're Building

```
tools/quality/
├── pipeline.py           ← main runner (CLI entry point)
├── tasks/
│   ├── __init__.py
│   ├── review.py         ← commit review task
│   ├── audit.py          ← safety audit task
│   ├── learn.py          ← pattern extraction task
│   └── teach.py          ← lesson generation task
├── prompts/
│   ├── review.md         ← prompt template for commit review
│   ├── audit.md          ← prompt template for safety audit
│   ├── learn.md          ← prompt template for pattern extraction
│   └── teach.md          ← prompt template for lesson generation
├── config.json           ← Ollama model, file patterns, RAG endpoints
├── requirements.txt
└── .env.example
```

Plus output directories (created at runtime):

```
reports/                  ← ephemeral review reports (gitignored)
knowledge/                ← permanent extracted knowledge (committed)
  patterns.md
  anti-patterns.md
  coupling-map.md
  lessons/
```

### pipeline.py — CLI Entry Point

```python
"""
Magic Quality Pipeline — Recurring code review, grading, and knowledge extraction.

Usage:
    python pipeline.py review [--since 24h]     # Review recent commits
    python pipeline.py audit                     # Safety audit firmware + daemon
    python pipeline.py learn                     # Extract patterns from codebase
    python pipeline.py teach <bug_commit> <fix_commit>  # Generate lesson from bug fix
    python pipeline.py all                       # Run review + audit + learn
    python pipeline.py ingest                    # Push reports/knowledge into RAG
"""

import argparse
import sys
from pathlib import Path

from tasks.review import ReviewTask
from tasks.audit import AuditTask
from tasks.learn import LearnTask
from tasks.teach import TeachTask

REPO_ROOT = Path(__file__).parent.parent.parent  # tools/quality/ -> repo root

def main():
    parser = argparse.ArgumentParser(description="Magic Quality Pipeline")
    subparsers = parser.add_subparsers(dest="command")

    review_p = subparsers.add_parser("review", help="Review recent commits")
    review_p.add_argument("--since", default="24h", help="Time range (default: 24h)")

    subparsers.add_parser("audit", help="Safety audit of firmware + daemon")
    subparsers.add_parser("learn", help="Extract patterns from codebase")

    teach_p = subparsers.add_parser("teach", help="Generate lesson from bug fix")
    teach_p.add_argument("bug_commit", help="Commit hash of the bug")
    teach_p.add_argument("fix_commit", help="Commit hash of the fix")

    subparsers.add_parser("all", help="Run review + audit + learn")
    subparsers.add_parser("ingest", help="Push knowledge into RAG")

    args = parser.parse_args()
    # ... dispatch to task classes
```

### Task Base Class

Each task follows the same pattern:

```python
class BaseTask:
    def __init__(self, repo_root: Path, config: dict):
        self.repo_root = repo_root
        self.config = config
        self.ollama_url = config.get("ollama_url", "http://localhost:11434")
        self.model = config.get("model", "qwen2.5-coder:14b")

    def gather(self) -> str:
        """Gather input context (git diff, file reads, etc.)"""
        raise NotImplementedError

    def prompt(self, context: str) -> str:
        """Load prompt template and inject context."""
        raise NotImplementedError

    def analyze(self, prompt: str) -> str:
        """Send to Ollama and get response."""
        # Use ollama_bridge pattern: POST /api/generate
        ...

    def save(self, result: str):
        """Save output to reports/ or knowledge/."""
        raise NotImplementedError

    def run(self):
        """Full pipeline: gather -> prompt -> analyze -> save."""
        context = self.gather()
        full_prompt = self.prompt(context)
        result = self.analyze(full_prompt)
        self.save(result)
```

### ReviewTask (tasks/review.py)

**gather():** Run `git log --since={since} -p` to get recent commit diffs.
**prompt():** Load `prompts/review.md`, inject the diff content.
**save():** Write to `reports/YYYY-MM-DD-commit-review.md`.

The review prompt must ask Ollama to:
1. Grade each file changed: A/B/C/D/F
2. List issues with severity (CRITICAL/HIGH/MEDIUM/LOW)
3. Identify good patterns worth preserving
4. Suggest lessons the team should learn

### AuditTask (tasks/audit.py)

**gather():** Read all `.cpp`, `.h` files in `firmware/magic/lib/` and all `.py` files in `daemon/src/`.
**prompt():** Load `prompts/audit.md` — focused on the ESP32/FreeRTOS safety checklist and Python async patterns.
**save():** Write to `reports/YYYY-MM-DD-safety-audit.md`.

The audit checklist:
- [ ] No `portENTER_CRITICAL()` called from ISR context (must use `_ISR` variant)
- [ ] No `portMAX_DELAY` passed to `pdMS_TO_TICKS()` (overflow on 32-bit)
- [ ] No blocking calls in async functions
- [ ] No `asyncio.QueueEmpty` (doesn't exist)
- [ ] No hardcoded secrets or broker addresses
- [ ] All MQTT topics match `magic/{node_id}/...` contract
- [ ] `memset` before `memcpy` on MxMessage structs
- [ ] Signal handlers are platform-safe (SIGTERM on Windows)

### LearnTask (tasks/learn.py)

**gather():** Read key architectural files:
- `firmware/magic/lib/Mx/mx_bus.cpp` — message bus
- `firmware/magic/lib/App/command_mx_bridge.cpp` — command routing
- `firmware/magic/lib/App/wifi_mx_adapter.cpp` — active object pattern
- `daemon/src/mqtt_client.py` — MQTT integration
- `daemon/src/lvc_service.py` — LVC cache pattern
- `plugins/test-pump/pump.py` — plugin pattern

**prompt():** Ask Ollama to extract:
1. Architectural patterns (active object, message bus, LVC)
2. Naming conventions (prefixes, suffixes, file organization)
3. Error handling patterns
4. MQTT topic contract (all topics and payload schemas)
5. Anti-patterns (things that caused bugs)
6. Coupling points (firmware <-> daemon <-> tools)

**save():** Write to `knowledge/patterns.md`, `knowledge/anti-patterns.md`, `knowledge/coupling-map.md`.

### TeachTask (tasks/teach.py)

**gather():** Given bug_commit and fix_commit hashes, get:
- `git show {bug_commit}` — the buggy code
- `git show {fix_commit}` — the fix
- `git log {bug_commit}..{fix_commit} --oneline` — context

**prompt():** Ask Ollama to generate a lesson:
1. What went wrong and WHY
2. Incorrect pattern vs correct pattern (side by side)
3. Checklist to prevent this class of bug
4. Clear lesson name

**save():** Write to `knowledge/lessons/YYYY-MM-DD-{slug}.md`.

### Ingest Command

When `pipeline.py ingest` is run:
1. Read all `.md` files from `knowledge/`
2. Chunk and embed via `tools/multi-agent-framework/rag/ingest.py`
3. Store in ChromaDB collection `magic-knowledge`

This makes all extracted knowledge queryable:
```bash
python -m rag.retriever --query "how does MxPool handle ISR safety?" --top-k 3
```

### Grading Rubric

| Grade | Meaning | Criteria |
|-------|---------|----------|
| **A** | Exemplary | No issues. Good patterns. Well-tested. |
| **B** | Solid | Minor style issues only. Functional and safe. |
| **C** | Acceptable | Medium issues present. Works but needs improvement. |
| **D** | Needs Work | High-severity issues. Missing error handling or tests. |
| **F** | Fail | Critical safety violation, hardcoded secrets, data loss risk. |

### config.json

```json
{
  "ollama_url": "http://localhost:11434",
  "model": "qwen2.5-coder:14b",
  "repo_root": "../..",
  "firmware_paths": ["firmware/magic/lib/"],
  "daemon_paths": ["daemon/src/"],
  "plugin_paths": ["plugins/"],
  "reports_dir": "../../reports",
  "knowledge_dir": "../../knowledge",
  "chromadb_collection": "magic-knowledge",
  "review_since_default": "24h"
}
```

### requirements.txt

```
httpx
python-dotenv
chromadb
```

### .env.example

```
OLLAMA_URL=http://localhost:11434
OLLAMA_MODEL=qwen2.5-coder:14b
```

### Prompt Templates

Store these as markdown files in `prompts/`. The task classes load them and inject context via `{placeholder}` substitution.

**prompts/review.md:**
```
You are a senior embedded systems and Python code reviewer for the Magic IoT platform.
This platform runs on ESP32-S3 (Heltec WiFi LoRa 32 V3) with FreeRTOS, and a Python daemon with asyncio.

Review the following commits against these standards:
- ESP32/FreeRTOS safety: no ISR violations, no portMAX_DELAY overflow, no cross-task mutex
- Python async safety: no blocking calls in async context, proper error handling
- MQTT contract compliance: topics match magic/{node_id}/telemetry format
- Immutability: new objects preferred over mutation
- Error handling: explicit, no silent swallows
- Security: no hardcoded secrets, validated inputs

For each file changed, output:
1. GRADE: A/B/C/D/F
2. ISSUES: list with severity (CRITICAL/HIGH/MEDIUM/LOW)
3. PATTERNS: good patterns worth preserving
4. LESSONS: what the team should learn

{context}
```

**prompts/audit.md:**
```
You are a safety auditor for the Magic IoT platform (ESP32-S3 + FreeRTOS + Python asyncio).

Audit the following source files against this safety checklist:
1. No portENTER_CRITICAL() from ISR context (must use _ISR variant)
2. No portMAX_DELAY passed to pdMS_TO_TICKS() (overflow on 32-bit)
3. No blocking calls in async functions
4. No asyncio.QueueEmpty (doesn't exist in Python)
5. No hardcoded secrets or broker addresses
6. All MQTT topics match magic/{node_id}/... contract
7. memset before memcpy on MxMessage structs
8. Signal handlers are platform-safe

For each file, output:
- PASS / FAIL per checklist item
- Specific line numbers for any failures
- Overall safety grade: A/B/C/D/F

{context}
```

**prompts/learn.md:**
```
You are analyzing the Magic IoT platform codebase to extract institutional knowledge.

Extract the following from these source files:
1. ARCHITECTURAL PATTERNS — recurring design patterns (active object, message bus, LVC, plugin model)
2. NAMING CONVENTIONS — prefixes, suffixes, file organization rules
3. ERROR HANDLING PATTERNS — how errors flow from firmware through daemon to UI
4. MQTT TOPIC CONTRACT — all topic patterns and their payload schemas
5. ANTI-PATTERNS — code patterns that caused bugs (with examples)
6. COUPLING POINTS — where firmware and daemon/tools must stay in sync

Output as structured markdown. Use code examples from the actual files where possible.

{context}
```

**prompts/teach.md:**
```
A bug was found and fixed in the Magic IoT platform.

Bug commit: {bug_commit}
Fix commit: {fix_commit}

{context}

Generate a permanent lesson document:
1. TITLE: Clear name for this lesson (e.g., "ISR Safety: Never call portENTER_CRITICAL from ISR context")
2. WHAT WENT WRONG: Plain English explanation
3. ROOT CAUSE: Technical details
4. INCORRECT PATTERN: Code that caused the bug
5. CORRECT PATTERN: Code that fixes it
6. PREVENTION CHECKLIST: Steps to catch this class of bug in code review
7. RELATED: Links to similar lessons if applicable
```

### ESP32/Safety Checklist for AG

- [ ] **No Ollama API calls are blocking the event loop** — all HTTP calls use `httpx` (async-capable) or run in a thread
- [ ] **Git subprocess calls use `subprocess.run`** with timeout — never hang on a git command
- [ ] **Prompt templates use `{placeholder}` substitution** — no f-strings with user data (injection risk)
- [ ] **Reports directory is created if missing** — `Path.mkdir(parents=True, exist_ok=True)`
- [ ] **Ollama timeout is generous** — 600s minimum for large diffs
- [ ] **UTF-8 encoding everywhere** — git diffs may contain non-ASCII
- [ ] **No hardcoded file paths** — everything relative to `config.json` `repo_root`
- [ ] **Graceful degradation** — if Ollama is not running, log error and skip (don't crash)

### What NOT To Do

1. **Do NOT modify `ollama_bridge.py`** — import and wrap it, or replicate its HTTP pattern
2. **Do NOT use asyncio** — this is a CLI tool, synchronous is correct
3. **Do NOT store reports in git** — `reports/` is gitignored. Only `knowledge/` is committed.
4. **Do NOT fine-tune or retrain Ollama models** — prompt engineering only
5. **Do NOT add dependencies beyond httpx, python-dotenv, and chromadb**

### Build Verification

```bash
# Verify structure
ls tools/quality/pipeline.py
ls tools/quality/tasks/review.py
ls tools/quality/prompts/review.md

# Verify Python syntax
python -c "import ast; ast.parse(open('tools/quality/pipeline.py').read())"

# Test help output
cd tools/quality
python pipeline.py --help

# Test review (requires Ollama running on localhost:11434)
python pipeline.py review --since 24h
```

### Success Criteria

1. `tools/quality/` directory exists with all files listed above
2. `pipeline.py --help` shows all subcommands
3. `pipeline.py review` produces a graded review report in `reports/`
4. `pipeline.py learn` extracts patterns into `knowledge/`
5. `pipeline.py teach` generates a lesson from two commit hashes
6. `pipeline.py ingest` pushes knowledge into ChromaDB
7. All 8 safety checklist items verified
8. Ollama not running → graceful error message, not crash

## PROMPT END
