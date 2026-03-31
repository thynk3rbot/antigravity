
# README_AI.md

## Purpose

This repository contains Magic firmware and associated tooling.  
AI agents working in this repository **must read the architecture and system prompt files first** before attempting analysis, refactoring, or code generation.

These files establish the authoritative architectural context and operating rules for agents.

---

# Required First Reads

Before doing anything else, agents must read these files:

1. **.ai/AGENT_SYSTEM_PROMPT.md**
2. **ARCHITECTURE_MAP.md**
3. **Magic AI Context file (AI Context V2)**

These files define:

- project architecture
- manager responsibilities
- command routing model
- board assumptions
- DevOps workflow
- tool coupling rules

Agents must treat these files as the **source of truth** for project structure.

---

# Repository Mental Model

Magic is:

> a Heltec-first embedded command-and-automation platform with multiple transports and centralized command routing.

Key architectural properties:

- manager-based singleton architecture
- prioritized any-to-any command routing
- TaskScheduler-driven runtime behavior
- Heltec WiFi LoRa 32 V3 hardware
- tightly coupled PC-side tooling

Agents must preserve this structure unless explicitly instructed otherwise.

---

# Critical Rule: Tools Are First-Class

The `tools/` directory is part of the product.

Firmware changes affecting:

- commands
- API endpoints
- pins
- scheduler limits
- BLE UUIDs
- ESP-NOW structures

**must also update the relevant files in `tools/`.**

Failure to do this will cause silent breakage.

---

# Development Workflow

Agents must follow these rules:

- never commit directly to `main`
- always work on `feature/<topic>` branches
- always create a PR back to `main`

OTA deployment:
- uses mDNS
- no hardcoded IP addresses

Versioning:
- manual
- controlled through `FIRMWARE_VERSION` in `src/config.h`

Build policy:
- single build
- multi-device flash
- never produce per-device firmware builds

---

# Agent Behavior Expectations

Agents should:

1. Inspect repository files before proposing changes.
2. Cite specific files when making recommendations.
3. Prefer incremental refactoring over large rewrites.
4. Maintain compatibility with existing managers and tools.
5. Explicitly identify architectural risks when suggesting changes.

Agents must **not**:

- invent hardware support
- ignore tooling coupling
- rewrite working architecture without justification

---

# Architectural Direction

The firmware may evolve toward:

- board support abstraction
- device classes
- feature profiles (modular transport stacks)
- operating modes
- command domain gating
- development-only diagnostics

However:

**Heltec WiFi LoRa 32 V3 remains the only supported board today.**

Design abstractions carefully without pretending other hardware exists.

---

# Quick Start for AI Agents

Before doing work:

1. Read `.ai/AGENT_SYSTEM_PROMPT.md`
2. Read `ARCHITECTURE_MAP.md`
3. Inspect `src/managers/`
4. Inspect `main.cpp`
5. Inspect `tools/`

Only then begin analysis or modification.

---

# Final Principle

Agents must behave as **repo-native architects**:

Understand what already exists, preserve what works, and evolve the platform carefully.
