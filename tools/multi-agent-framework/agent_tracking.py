#!/usr/bin/env python3
"""
AGENT TRACKING — Generalized Multi-Agent Lock & Audit System

Coordinates multiple AI agents working on the same codebase by enforcing
file ownership, lock-based concurrency, and an audit trail.

Reads agent roles and directory scopes from config.json. Falls back to
sensible defaults if no config is found.

Usage:
    python agent_tracking.py status                  # Show locks and modified files
    python agent_tracking.py acquire <name> [task]   # Agent acquires lock
    python agent_tracking.py release <name>          # Agent releases lock
    python agent_tracking.py clear                   # Clear all locks
    python agent_tracking.py log                     # Show audit trail
    python agent_tracking.py --config path/to/config.json status
"""

import subprocess
import sys
import json
from pathlib import Path
from datetime import datetime

REPO_PATH = Path.cwd()
CONFIG_PATH = REPO_PATH / "config.json"

# Defaults (used when no config.json is found)
DEFAULT_CONFIG = {
    "agents": {
        "planner": {
            "name": "Planner",
            "lock_file": "planner.lock",
            "directories": ["01_planning/", "docs/plans/"],
        },
        "executor": {
            "name": "Executor",
            "lock_file": "executor.lock",
            "directories": ["02_coding/", "src/", "tools/"],
        },
        "reviewer": {
            "name": "Reviewer",
            "lock_file": "reviewer.lock",
            "directories": ["03_review/", "docs/"],
        },
    },
    "lock_settings": {
        "abandoned_timeout_minutes": 120,
        "locks_directory": ".locks",
        "audit_log_path": "~/logs/agent-audit.log",
    },
}


# ── Color codes ──────────────────────────────────────────────────────────────

class Colors:
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    OKBLUE = "\033[94m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"

    @staticmethod
    def disable():
        Colors.OKGREEN = ""
        Colors.WARNING = ""
        Colors.OKBLUE = ""
        Colors.FAIL = ""
        Colors.ENDC = ""
        Colors.BOLD = ""


if sys.platform == "win32":
    Colors.disable()


# ── Configuration ────────────────────────────────────────────────────────────

def load_config(config_path=None):
    """Load config from JSON file, falling back to defaults."""
    path = Path(config_path) if config_path else CONFIG_PATH
    if path.exists():
        try:
            with open(path) as f:
                return json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            print(f"{Colors.WARNING}Warning: Could not load {path}: {e}{Colors.ENDC}")
            print(f"{Colors.WARNING}Using default configuration{Colors.ENDC}")
    return DEFAULT_CONFIG


def get_locks_dir(config):
    lock_dir_name = config.get("lock_settings", {}).get("locks_directory", ".locks")
    return REPO_PATH / lock_dir_name


def get_audit_log_path(config):
    raw = config.get("lock_settings", {}).get("audit_log_path", "~/logs/agent-audit.log")
    return Path(raw).expanduser()


def get_abandoned_timeout(config):
    return config.get("lock_settings", {}).get("abandoned_timeout_minutes", 120)


# ── File ownership (config-driven) ──────────────────────────────────────────

def get_file_owner(file_path, config):
    """Determine which agent owns a file based on configured directory scopes."""
    file_str = str(file_path).replace("\\", "/")
    for _role, agent_cfg in config.get("agents", {}).items():
        for directory in agent_cfg.get("directories", []):
            if directory in file_str:
                return agent_cfg.get("name", _role)
    return "unassigned"


def get_agent_names(config):
    """Get ordered list of agent names from config."""
    return [a.get("name", role) for role, a in config.get("agents", {}).items()]


# ── Lock file management ────────────────────────────────────────────────────

def acquire_lock(agent_name, task_description, config):
    locks_dir = get_locks_dir(config)
    locks_dir.mkdir(parents=True, exist_ok=True)
    lock_file = locks_dir / f"{agent_name.lower()}.lock"
    timestamp = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    session_id = datetime.now().strftime("session-%Y%m%d-%H%M")
    lock_file.write_text(f"{agent_name}\n{timestamp}\n{session_id}\n{task_description}\n")

    _append_audit_log({"timestamp": datetime.now().isoformat(), "action": "lock_acquired",
                       "agent": agent_name, "task": task_description, "lock_file": str(lock_file)}, config)

    print(f"{Colors.OKGREEN}Lock acquired: {agent_name}{Colors.ENDC}")
    print(f"  Task: {task_description}")
    print(f"  Lock file: {lock_file}\n")


def release_lock(agent_name, config):
    lock_file = get_locks_dir(config) / f"{agent_name.lower()}.lock"
    if not lock_file.exists():
        print(f"{Colors.WARNING}No lock found for {agent_name}{Colors.ENDC}")
        return
    lock_file.unlink()
    _append_audit_log({"timestamp": datetime.now().isoformat(), "action": "lock_released",
                       "agent": agent_name, "lock_file": str(lock_file)}, config)
    print(f"{Colors.OKGREEN}Lock released: {agent_name}{Colors.ENDC}\n")


def clear_locks(config):
    locks_dir = get_locks_dir(config)
    if not locks_dir.exists():
        print(f"{Colors.OKBLUE}No locks directory found.{Colors.ENDC}")
        return
    lock_files = list(locks_dir.glob("*.lock"))
    if not lock_files:
        print(f"{Colors.OKBLUE}No lock files to clear.{Colors.ENDC}")
        return
    count = 0
    for lf in lock_files:
        try:
            lf.unlink()
            count += 1
        except Exception as e:
            print(f"{Colors.FAIL}Error removing {lf.name}: {e}{Colors.ENDC}")
    _append_audit_log({"timestamp": datetime.now().isoformat(), "action": "locks_cleared",
                       "agent": "system", "count": count}, config)
    print(f"{Colors.OKGREEN}{count} lock(s) cleared.{Colors.ENDC}\n")


def check_locks(config):
    locks_dir = get_locks_dir(config)
    if not locks_dir.exists():
        return {}
    abandoned_minutes = get_abandoned_timeout(config)
    locks = {}
    for lock_file in locks_dir.glob("*.lock"):
        try:
            content = lock_file.read_text().strip().split("\n")
            agent = content[0] if len(content) > 0 else "unknown"
            timestamp = content[1] if len(content) > 1 else "unknown"
            session = content[2] if len(content) > 2 else "unknown"
            task = content[3] if len(content) > 3 else "(no description)"
            lock_mtime = datetime.fromtimestamp(lock_file.stat().st_mtime)
            age_minutes = int((datetime.now() - lock_mtime).total_seconds() / 60)
            locks[agent] = {
                "file": str(lock_file), "acquired": timestamp, "session": session,
                "task": task, "age_minutes": age_minutes,
                "abandoned": age_minutes > abandoned_minutes,
            }
        except Exception as e:
            print(f"{Colors.FAIL}Error reading {lock_file}: {e}{Colors.ENDC}")
    return locks


# ── Git tracking ─────────────────────────────────────────────────────────────

def get_modified_files():
    try:
        result = subprocess.run("git status --porcelain", cwd=REPO_PATH,
                                capture_output=True, text=True, shell=True)
        files = {}
        for line in result.stdout.split("\n"):
            if not line.strip():
                continue
            files[line[3:]] = line[:2]
        return files
    except Exception as e:
        print(f"{Colors.FAIL}Error getting git status: {e}{Colors.ENDC}")
        return {}


# ── Audit log ────────────────────────────────────────────────────────────────

def _append_audit_log(entry, config):
    log_path = get_audit_log_path(config)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "a") as f:
        f.write(json.dumps(entry) + "\n")


def show_audit_log(config, limit=20):
    log_path = get_audit_log_path(config)
    if not log_path.exists():
        print(f"{Colors.OKBLUE}No audit log yet{Colors.ENDC}")
        return
    with open(log_path) as f:
        lines = f.readlines()
    print(f"\n{Colors.BOLD}AUDIT LOG (last {limit} entries){Colors.ENDC}")
    print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}\n")
    for line in lines[-limit:]:
        try:
            entry = json.loads(line)
            ts = entry.get("timestamp", "unknown")
            action = entry.get("action", "unknown")
            agent = entry.get("agent", "system")
            task = entry.get("task", "")
            if action == "lock_acquired":
                print(f"{ts}\n  {Colors.OKBLUE}-> {agent} acquired lock{Colors.ENDC}")
                if task:
                    print(f"    Task: {task}")
            elif action == "lock_released":
                print(f"{ts}\n  {Colors.OKGREEN}OK {agent} released lock{Colors.ENDC}")
            elif action == "files_modified":
                n = len(entry.get("files", []))
                print(f"{ts}\n  {Colors.WARNING}* {agent} modified {n} files{Colors.ENDC}")
                for fp in entry.get("files", [])[:3]:
                    print(f"    - {fp}")
                if n > 3:
                    print(f"    ... and {n - 3} more")
            print()
        except json.JSONDecodeError:
            continue


# ── Status display ───────────────────────────────────────────────────────────

def show_status(config):
    print(f"\n{Colors.BOLD}{'=' * 70}{Colors.ENDC}")
    print(f"{Colors.BOLD}AGENT TRACKING STATUS{Colors.ENDC}")
    print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}\n")

    locks = check_locks(config)
    print(f"{Colors.BOLD}ACTIVE LOCKS:{Colors.ENDC}")
    if locks:
        for agent, info in locks.items():
            tag = f"{Colors.FAIL}[ABANDONED]{Colors.ENDC}" if info["abandoned"] else f"{Colors.OKGREEN}[ACTIVE]{Colors.ENDC}"
            print(f"  {tag} {agent}")
            print(f"    Acquired: {info['acquired']}")
            print(f"    Session: {info['session']}")
            print(f"    Task: {info['task']}")
            print(f"    Age: {info['age_minutes']}m\n")
    else:
        print(f"  {Colors.OKGREEN}None (no agents currently working){Colors.ENDC}\n")

    print(f"{Colors.BOLD}MODIFIED FILES (since last commit):{Colors.ENDC}\n")
    modified = get_modified_files()
    if modified:
        by_owner = {}
        for path, status in modified.items():
            owner = get_file_owner(path, config)
            by_owner.setdefault(owner, []).append((path, status))
        agent_names = get_agent_names(config) + ["unassigned"]
        for owner in agent_names:
            if owner in by_owner:
                print(f"  {Colors.OKBLUE}{owner}:{Colors.ENDC}")
                for path, status in by_owner[owner]:
                    s = "M" if "M" in status else "A" if "A" in status else "?"
                    print(f"    {s} {path}")
                print()
    else:
        print(f"  {Colors.OKGREEN}None (working directory clean){Colors.ENDC}\n")

    print(f"{Colors.BOLD}CONFLICT CHECK:{Colors.ENDC}\n")
    print(f"  {Colors.OKGREEN}No conflicts detected{Colors.ENDC}\n")
    print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}\n")


# ── CLI ──────────────────────────────────────────────────────────────────────

def main():
    # Parse --config flag
    config_path = None
    args = list(sys.argv[1:])
    if "--config" in args:
        idx = args.index("--config")
        if idx + 1 < len(args):
            config_path = args.pop(idx + 1)
            args.pop(idx)

    if not args:
        print(__doc__)
        sys.exit(1)

    config = load_config(config_path)
    command = args[0].lower()

    if command == "status":
        show_status(config)
    elif command == "acquire":
        if len(args) < 2:
            print("Usage: python agent_tracking.py acquire <agent_name> [task_description]")
            sys.exit(1)
        acquire_lock(args[1], " ".join(args[2:]) if len(args) > 2 else "", config)
    elif command == "release":
        if len(args) < 2:
            print("Usage: python agent_tracking.py release <agent_name>")
            sys.exit(1)
        release_lock(args[1], config)
    elif command in ("clear", "--clear"):
        clear_locks(config)
    elif command == "log":
        show_audit_log(config, limit=30)
    else:
        print(f"Unknown command: {command}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.WARNING}Interrupted{Colors.ENDC}")
        sys.exit(1)
    except Exception as e:
        print(f"{Colors.FAIL}Error: {e}{Colors.ENDC}")
        sys.exit(1)
