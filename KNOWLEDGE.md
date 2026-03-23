
## [2026-03-23 10:54:37] Firmware V2 Regression Fixes

**Technical Context:**
Identified and fixed critical regressions in v2 port: 1. Missing 5s boot stabilization delay causing brownouts. 2. Missing 1.6V TCXO and LDO configuration for SX1262 (Heltec V3/V4). 3. Incorrect Radio Sync Word (0x12). 4. WebApp dictionary mutation and DOM thrashing issues.

**Actionable Rule:**
- [ ] Added to AGENTS.md / PROCESSES.md
- [ ] Verified in current branch

---
