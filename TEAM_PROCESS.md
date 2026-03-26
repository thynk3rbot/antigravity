# LoRaLink Team Process
**Three agents. One repo. No blocking.**

---

## The Team

| Agent | Tool | Owns |
|-------|------|------|
| **Claude** | Claude Code (this session) | Architecture, release engineering, daemon, tools, specs |
| **AG** | Antigravity IDE | Firmware coding, hardware flashing, test validation |
| **Ollama** | qwen2.5-coder:14b @ localhost:11434 | Async code generation, boilerplate, search/replace, analysis |

---

## The One Rule

**Queue Ollama tasks at the START of the session, not the end.**

If you need generated code in an hour, queue it now and work on something else.
Ollama is free, local, and always running. Don't hand-write what it can generate.

---

## Session Start Checklist (AG)

```
1. git pull origin main           ← Always start from main
2. Queue async Ollama tasks now   ← Don't wait
3. Flash + test while Ollama runs ← No blocking
4. Report results to Claude       ← Even one line: "OLED fix confirmed V3"
5. Claude commits + bumps version ← You never touch git
```

---

## Session Start Checklist (Claude)

```
1. git status                     ← Nothing uncommitted from last session
2. Review any Ollama output ready ← Integrate it
3. Queue new Ollama tasks         ← For things needed this session
4. Code daemon/tools in parallel  ← While AG flashes hardware
5. End of session: commit, tag    ← Always. No exceptions.
```

---

## How to Queue an Ollama Task

From `firmware/v2/` or `tools/` directory:

```bash
# Generate boilerplate code
python ../../tools/multi-agent-framework/ollama_bridge.py generate-code \
  "Write a C++ function to parse a ControlPacket from a JSON string. Fields: cmd_id, action, pin, value. Return struct ControlPacket."

# Search and replace across a file
python ../../tools/multi-agent-framework/ollama_bridge.py search-replace \
  "In this code, replace all direct Serial.println calls with LOG_INFO macro calls"

# Analyze for bugs
python ../../tools/multi-agent-framework/ollama_bridge.py analyze \
  "Review this MQTT topic handler for race conditions: [paste code]"

# Refactor
python ../../tools/multi-agent-framework/ollama_bridge.py refactor \
  "Refactor this switch statement into a dispatch table"
```

**Save output to a file for later:**
```bash
python ../../tools/multi-agent-framework/ollama_bridge.py generate-code \
  "Write test stubs for CommandManager" > /tmp/command_manager_tests.cpp
```

---

## What to Queue to Ollama (AG: Don't Hand-Write These)

| Task | Queue It |
|------|----------|
| C++ struct definitions | ✅ |
| JSON serialization boilerplate | ✅ |
| Test stubs and scaffolding | ✅ |
| Repetitive switch/case handlers | ✅ |
| CSS, HTML templates | ✅ |
| SQLite CRUD functions | ✅ |
| Python route stubs (FastAPI) | ✅ |
| Documentation comments | ✅ |
| Search/replace across files | ✅ |

**AG decides the architecture. Ollama writes the boilerplate. AG reviews and integrates.**

---

## How Commits Work

**AG never commits.** Claude owns all git operations.

When AG has a result:
> "VSTATUS verified on V3, boot stable 10 minutes"

Claude does:
```
git add -A
git commit -m "fix: VSTATUS verified on V3 hardware — 10min uptime clean"
git tag v0.0.15-v3
git push origin main --tags
```

This means every flash has a tag. Every tag maps to a version number on the device.
If `STATUS` returns `0.0.15` and the tag is `v0.0.15-v3`, the flash worked.

---

## Version = Flash Confirmation

Version increments automatically on every `pio run -t upload`.

```
Before flash: STATUS → {"ver": "0.0.14", ...}
After flash:  STATUS → {"ver": "0.0.15", ...}
```

If version didn't change → flash failed. Try again.

---

## Parallel Work Pattern

```
Claude                    AG                        Ollama
──────                    ──────                    ──────
Queue tasks to Ollama  →  Pull main                Queue draining...
Code daemon endpoint      Flash v0.0.15 to V3
                          Monitor serial            ↓ Output ready
Integrate Ollama output ← Report: "V3 stable"      Results available
Commit + tag              Validate STATUS response
```

**Nothing blocks. No one waits for anyone else to finish.**

---

## Daily Sync (5 minutes, async-friendly)

No meeting needed. Just update `docs/AGENT_RADIO.md`:

```markdown
## [DATE] Status
- Claude: [what I did / what I queued to Ollama]
- AG: [what hardware was tested / what version is running]
- Ollama: [what was generated / what's queued]
- Blocked on: [anything]
- Next: [what each agent does next]
```

---

## When Things Break

1. AG reports the symptom + version that broke it
2. Claude checks git log for what changed between working and broken version
3. Claude queues Ollama to analyze the suspect code
4. AG tests the fix
5. Claude commits the fix with the version that broke + version that fixed

**Never debug without knowing the version. That's what the version number is for.**
