#!/usr/bin/env python3
"""
log_lesson.py — LoRaLink Persistence Utility
Captures technical lessons learned to prevent regressions.
Usage: python tools/log_lesson.py "Summary of lesson" "Detailed technical reasoning"
"""

import sys
import datetime
import os

LOG_FILE = "KNOWLEDGE.md"

def log_lesson(summary, details):
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    entry = f"""
## [{timestamp}] {summary}

**Technical Context:**
{details}

**Actionable Rule:**
- [ ] Added to AGENTS.md / PROCESSES.md
- [ ] Verified in current branch

---
"""
    
    with open(LOG_FILE, "a") as f:
        f.write(entry)
    
    print(f"✅ Lesson logged to {LOG_FILE}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python tools/log_lesson.py \"Summary\" \"Detailed Context\"")
        sys.exit(1)
    
    log_lesson(sys.argv[1], sys.argv[2])
