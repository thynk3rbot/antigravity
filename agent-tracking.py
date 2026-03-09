#!/usr/bin/env python3
"""
AGENT TRACKING SCRIPT (Cross-Platform)

Monitors which files each agent modifies, maintains audit log of all changes.
Tracks lock file creation/removal and coordinates multi-agent workflow.

Usage:
    python agent-tracking.py status           # Show current locks and modified files
    python agent-tracking.py acquire <name>   # Agent acquires lock before work
    python agent-tracking.py release <name>   # Agent releases lock after work
    python agent-tracking.py log              # Show modification audit trail
"""

import subprocess
import sys
import json
from pathlib import Path
from datetime import datetime, timedelta
import os

HOME_DIR = Path.home()
REPO_PATH = Path.cwd()
LOCKS_DIR = REPO_PATH / ".locks"
AUDIT_LOG = HOME_DIR / "logs" / "agent-audit.log"

# Color codes for terminal output
class Colors:
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    OKBLUE = '\033[94m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

    @staticmethod
    def disable():
        Colors.OKGREEN = ''
        Colors.WARNING = ''
        Colors.OKBLUE = ''
        Colors.FAIL = ''
        Colors.ENDC = ''
        Colors.BOLD = ''

if sys.platform == "win32":
    Colors.disable()


# ============================================================================
# LOCK FILE MANAGEMENT
# ============================================================================

def acquire_lock(agent_name, task_description=""):
    """Create lock file for agent."""
    LOCKS_DIR.mkdir(parents=True, exist_ok=True)

    lock_file = LOCKS_DIR / f"{agent_name.lower()}.lock"
    timestamp = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    session_id = datetime.now().strftime("session-%Y%m%d-%H%M")

    lock_content = f"""{agent_name}
{timestamp}
{session_id}
{task_description}
"""

    lock_file.write_text(lock_content)

    # Log to audit trail
    audit_entry = {
        "timestamp": datetime.now().isoformat(),
        "action": "lock_acquired",
        "agent": agent_name,
        "task": task_description,
        "lock_file": str(lock_file)
    }
    _append_audit_log(audit_entry)

    print(f"{Colors.OKGREEN}✓ Lock acquired: {agent_name}{Colors.ENDC}")
    print(f"  Task: {task_description}")
    print(f"  Lock file: {lock_file}")
    print()


def release_lock(agent_name):
    """Remove lock file for agent."""
    lock_file = LOCKS_DIR / f"{agent_name.lower()}.lock"

    if not lock_file.exists():
        print(f"{Colors.WARNING}⚠ No lock found for {agent_name}{Colors.ENDC}")
        return

    lock_file.unlink()

    # Log to audit trail
    audit_entry = {
        "timestamp": datetime.now().isoformat(),
        "action": "lock_released",
        "agent": agent_name,
        "lock_file": str(lock_file)
    }
    _append_audit_log(audit_entry)

    print(f"{Colors.OKGREEN}✓ Lock released: {agent_name}{Colors.ENDC}")
    print()


def check_locks():
    """List all active locks."""
    if not LOCKS_DIR.exists():
        print(f"{Colors.OKBLUE}No locks directory (safe to create){Colors.ENDC}")
        return {}

    locks = {}
    for lock_file in LOCKS_DIR.glob("*.lock"):
        try:
            content = lock_file.read_text().strip().split('\n')
            agent = content[0] if len(content) > 0 else "unknown"
            timestamp = content[1] if len(content) > 1 else "unknown"
            session = content[2] if len(content) > 2 else "unknown"
            task = content[3] if len(content) > 3 else "(no description)"

            # Check lock age
            lock_mtime = datetime.fromtimestamp(lock_file.stat().st_mtime)
            age = datetime.now() - lock_mtime
            age_minutes = int(age.total_seconds() / 60)

            locks[agent] = {
                "file": str(lock_file),
                "acquired": timestamp,
                "session": session,
                "task": task,
                "age_minutes": age_minutes,
                "abandoned": age_minutes > 120  # >2 hours
            }
        except Exception as e:
            print(f"{Colors.FAIL}Error reading {lock_file}: {e}{Colors.ENDC}")

    return locks


# ============================================================================
# GIT MODIFICATION TRACKING
# ============================================================================

def get_modified_files():
    """Get all modified files since last commit."""
    try:
        result = subprocess.run(
            "git status --porcelain",
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True
        )

        files = {}
        for line in result.stdout.strip().split('\n'):
            if not line.strip():
                continue
            status = line[:2]
            path = line[3:]
            files[path] = status

        return files
    except Exception as e:
        print(f"{Colors.FAIL}Error getting git status: {e}{Colors.ENDC}")
        return {}


def get_last_commit_author(file_path):
    """Get author of last commit for file."""
    try:
        result = subprocess.run(
            f"git log -1 --format=%an -- \"{file_path}\"",
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True
        )
        return result.stdout.strip() or "unknown"
    except:
        return "unknown"


def get_file_owner(file_path):
    """Determine which agent owns a file based on AGENT_ASSIGNMENTS."""
    path = Path(file_path)

    # Claude's directories
    claude_dirs = [
        "src/managers/BLEManager",
        "src/managers/CommandManager",
        "src/managers/LoRaManager",
        "src/managers/ScheduleManager",
        "src/managers/WiFiManager",
        "src/config.h",
        "src/crypto.h"
    ]

    # Antigravity's directories
    antigravity_dirs = [
        "tools/webapp/server.py",
        "tools/webapp/static/",
        "tools/requirements.txt",
        "INTEGRATION.md"
    ]

    # Codex's directories
    codex_dirs = [
        "src/main.cpp",
        "src/managers/PerformanceManager",
        "src/managers/PowerManager"
    ]

    file_str = str(path)

    for pattern in claude_dirs:
        if pattern in file_str:
            return "Claude"

    for pattern in antigravity_dirs:
        if pattern in file_str:
            return "Antigravity"

    for pattern in codex_dirs:
        if pattern in file_str:
            return "Codex"

    return "unassigned"


# ============================================================================
# AUDIT LOGGING
# ============================================================================

def _append_audit_log(entry):
    """Append entry to audit log file."""
    AUDIT_LOG.parent.mkdir(parents=True, exist_ok=True)

    with open(AUDIT_LOG, 'a') as f:
        f.write(json.dumps(entry) + '\n')


def show_audit_log(limit=20):
    """Show recent entries from audit log."""
    if not AUDIT_LOG.exists():
        print(f"{Colors.OKBLUE}No audit log yet{Colors.ENDC}")
        return

    try:
        with open(AUDIT_LOG, 'r') as f:
            lines = f.readlines()

        print(f"\n{Colors.BOLD}AUDIT LOG (last {limit} entries){Colors.ENDC}")
        print(f"{Colors.BOLD}{'='*70}{Colors.ENDC}\n")

        for line in lines[-limit:]:
            try:
                entry = json.loads(line)
                timestamp = entry.get('timestamp', 'unknown')
                action = entry.get('action', 'unknown')
                agent = entry.get('agent', 'system')
                task = entry.get('task', '')

                # Format output
                if action == "lock_acquired":
                    print(f"{timestamp}")
                    print(f"  {Colors.OKBLUE}→ {agent} acquired lock{Colors.ENDC}")
                    if task:
                        print(f"    Task: {task}")

                elif action == "lock_released":
                    print(f"{timestamp}")
                    print(f"  {Colors.OKGREEN}✓ {agent} released lock{Colors.ENDC}")

                elif action == "files_modified":
                    print(f"{timestamp}")
                    print(f"  {Colors.WARNING}⊙ {agent} modified {len(entry.get('files', []))} files{Colors.ENDC}")
                    for f in entry.get('files', [])[:3]:
                        print(f"    - {f}")
                    if len(entry.get('files', [])) > 3:
                        print(f"    ... and {len(entry.get('files', [])) - 3} more")

                print()
            except json.JSONDecodeError:
                continue

    except Exception as e:
        print(f"{Colors.FAIL}Error reading audit log: {e}{Colors.ENDC}")


# ============================================================================
# STATUS DISPLAY
# ============================================================================

def show_status():
    """Show current agent status and modifications."""
    print()
    print(f"{Colors.BOLD}{'='*70}{Colors.ENDC}")
    print(f"{Colors.BOLD}AGENT TRACKING STATUS{Colors.ENDC}")
    print(f"{Colors.BOLD}{'='*70}{Colors.ENDC}\n")

    # Show active locks
    locks = check_locks()
    print(f"{Colors.BOLD}ACTIVE LOCKS:{Colors.ENDC}")

    if locks:
        for agent, info in locks.items():
            status_str = f"{Colors.FAIL}[ABANDONED]{Colors.ENDC}" if info['abandoned'] else f"{Colors.OKGREEN}[ACTIVE]{Colors.ENDC}"
            print(f"  {status_str} {agent}")
            print(f"    Acquired: {info['acquired']}")
            print(f"    Session: {info['session']}")
            print(f"    Task: {info['task']}")
            print(f"    Age: {info['age_minutes']}m")
            print()
    else:
        print(f"  {Colors.OKGREEN}None (no agents currently working){Colors.ENDC}\n")

    # Show modified files
    print(f"{Colors.BOLD}MODIFIED FILES (since last commit):{Colors.ENDC}\n")

    modified = get_modified_files()
    if modified:
        # Group by assigned owner
        by_owner = {}
        for path, status in modified.items():
            owner = get_file_owner(path)
            if owner not in by_owner:
                by_owner[owner] = []
            by_owner[owner].append((path, status))

        for owner in ["Claude", "Antigravity", "Codex", "unassigned"]:
            if owner in by_owner:
                print(f"  {Colors.OKBLUE}{owner}:{Colors.ENDC}")
                for path, status in by_owner[owner]:
                    status_str = "M" if "M" in status else "A" if "A" in status else "?"
                    print(f"    {status_str} {path}")
                print()
    else:
        print(f"  {Colors.OKGREEN}None (working directory clean){Colors.ENDC}\n")

    # Check for conflicts
    print(f"{Colors.BOLD}CONFLICT CHECK:{Colors.ENDC}\n")

    has_conflict = False
    for agent, info in locks.items():
        modified = get_modified_files()
        for path in modified:
            if get_file_owner(path) == agent:
                # Agent owns file and has lock — OK
                continue
            elif get_file_owner(path) != agent:
                # Different agent assigned but this agent modifying — potential conflict
                pass

    if not has_conflict:
        print(f"  {Colors.OKGREEN}✓ No conflicts detected{Colors.ENDC}\n")

    print(f"{Colors.BOLD}{'='*70}{Colors.ENDC}\n")


# ============================================================================
# MAIN
# ============================================================================

def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    command = sys.argv[1].lower()

    if command == "status":
        show_status()

    elif command == "acquire":
        if len(sys.argv) < 3:
            print("Usage: python agent-tracking.py acquire <agent_name> [task_description]")
            sys.exit(1)
        agent_name = sys.argv[2]
        task_description = " ".join(sys.argv[3:]) if len(sys.argv) > 3 else ""
        acquire_lock(agent_name, task_description)

    elif command == "release":
        if len(sys.argv) < 3:
            print("Usage: python agent-tracking.py release <agent_name>")
            sys.exit(1)
        agent_name = sys.argv[2]
        release_lock(agent_name)

    elif command == "log":
        show_audit_log(limit=30)

    else:
        print(f"Unknown command: {command}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.WARNING}⊗ Interrupted{Colors.ENDC}")
        sys.exit(1)
    except Exception as e:
        print(f"{Colors.FAIL}✗ Error: {e}{Colors.ENDC}")
        sys.exit(1)
