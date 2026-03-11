#!/usr/bin/env python3
"""
MERGE-TO-GITHUB SCRIPT (Cross-Platform)

Detects version changes in .h files and consolidates all agent work to GitHub.
Runs automatically on version change, or manually triggered by user.

Usage:
    python merge-to-github.py detect              # Check if version changed
    python merge-to-github.py consolidate         # Consolidate all agent work
    python merge-to-github.py --auto-push         # Consolidate and push to GitHub
    python merge-to-github.py --auto-increment    # Auto-increment patch version
"""

import subprocess
import sys
import re
from pathlib import Path
from datetime import datetime

REPO_PATH = Path.cwd()
CONFIG_H = REPO_PATH / "src" / "config.h"
VERSION_REGEX = r'#define FIRMWARE_VERSION "v((\d+)\.(\d+)\.(\d+))"'


# Color codes
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


# ============================================================================
# VERSION DETECTION
# ============================================================================


def get_version_from_config():
    """Read firmware version from src/config.h"""
    try:
        content = CONFIG_H.read_text()
        match = re.search(VERSION_REGEX, content)
        if match:
            return match.group(1)
        else:
            return None
    except Exception as e:
        print(f"{Colors.FAIL}Error reading config.h: {e}{Colors.ENDC}")
        return None


def get_version_from_git():
    """Get last version from git tag or commit message."""
    try:
        # Try to get from git tag
        result = subprocess.run(
            "git describe --tags --abbrev=0",
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True,
        )

        if result.returncode == 0:
            tag = result.stdout.strip()
            # Extract version from tag (e.g., "v0.0.1")
            match = re.search(r"v(\d+\.\d+\.\d+)", tag)
            if match:
                return match.group(1)

        # Fallback: check recent commits for version
        result = subprocess.run(
            "git log --oneline -20",
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True,
        )

        for line in result.stdout.split("\n"):
            match = re.search(r"v(\d+\.\d+\.\d+)", line)
            if match:
                return match.group(1)

        return None
    except Exception:
        return None


def detect_version_change():
    """Check if version has changed since last commit."""
    current = get_version_from_config()
    previous = get_version_from_git()

    if current is None:
        print(
            f"{Colors.FAIL}Could not read current version from src/config.h{Colors.ENDC}"
        )
        return False

    if previous is None:
        print(f"{Colors.WARNING}No previous version found (first merge?){Colors.ENDC}")
        return True

    if current != previous:
        print(
            f"{Colors.OKGREEN}✓ Version change detected: {previous} → {current}{Colors.ENDC}"
        )
        return True
    else:
        print(f"{Colors.OKBLUE}No version change ({current}){Colors.ENDC}")
        return False


# ============================================================================
# CONSOLIDATION
# ============================================================================


def get_agent_locks():
    """Read all active agent lock files."""
    locks_dir = REPO_PATH / ".locks"
    agents = {}

    if not locks_dir.exists():
        return agents

    for lock_file in locks_dir.glob("*.lock"):
        try:
            content = lock_file.read_text().strip().split("\n")
            agent = content[0] if len(content) > 0 else "unknown"
            task = content[3] if len(content) > 3 else "(no description)"

            agents[agent] = {"lock_file": str(lock_file), "task": task}
        except Exception:
            pass

    return agents


def get_modified_files_by_agent():
    """Group all modified files by assigned agent owner."""
    try:
        # Include both staged and unstaged
        result2 = subprocess.run(
            "git status --porcelain",
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True,
        )

        files_by_agent = {}

        lines = result2.stdout.split("\n")
        for line in lines:
            if not line.strip():
                continue

            # porcelain format has 3 char prefix (XY )
            path = line[3:]

            # Determine owner (sync'd with AGENT_ASSIGNMENTS.md)
            owner = "unassigned"
            if (
                "src/managers/" in path
                or "src/config.h" in path
                or "src/crypto.h" in path
                or "src/main.cpp" in path
            ):
                owner = "Antigravity"
            elif (
                "tools/webapp/" in path
                or "tools/pc_app/" in path
                or "docs/" in path
                or "INTEGRATION.md" in path
            ):
                owner = "Claude"
            elif "PerformanceManager" in path or "PowerManager" in path:
                owner = "Codex"

            if owner not in files_by_agent:
                files_by_agent[owner] = []

            files_by_agent[owner].append(path)

        return files_by_agent
    except Exception as e:
        print(f"{Colors.FAIL}Error getting modified files: {e}{Colors.ENDC}")
        return {}


def create_consolidation_commit(current_version, previous_version):
    """Create commit that consolidates all agent work."""
    files_by_agent = get_modified_files_by_agent()

    print(f"\n{Colors.BOLD}Preparing consolidation commit...{Colors.ENDC}\n")

    # Build commit message
    commit_lines = [
        f"consolidate: v{previous_version} → v{current_version} (all agents)",
        "",
        "Agent contributions:",
    ]

    for agent in ["Claude", "Antigravity", "Codex"]:
        if agent in files_by_agent:
            files = files_by_agent[agent]
            count = len(files)
            commit_lines.append(f"  {agent}: {count} file(s) modified")
            for f in files[:3]:
                commit_lines.append(f"    • {f}")
            if count > 3:
                commit_lines.append(f"    ... and {count - 3} more")

    commit_lines.extend(
        [
            "",
            "Lock files cleared. Session summary in logs/.",
            "",
            f"Auto-generated on {datetime.now().isoformat()}",
        ]
    )

    commit_message = "\n".join(commit_lines)

    # Stage all changes
    print(f"{Colors.OKBLUE}Staging all changes...{Colors.ENDC}")
    subprocess.run("git add -A", cwd=REPO_PATH, shell=True)

    # Create commit
    print(f"{Colors.OKBLUE}Creating consolidation commit...{Colors.ENDC}")

    # Write message to temp file to avoid shell escaping issues
    msg_file = REPO_PATH / ".consolidate_msg.txt"
    msg_file.write_text(commit_message)

    try:
        result = subprocess.run(
            f'git commit -F "{msg_file}"',
            cwd=REPO_PATH,
            capture_output=True,
            text=True,
            shell=True,
        )

        if result.returncode == 0:
            print(f"{Colors.OKGREEN}✓ Consolidation commit created{Colors.ENDC}")

            # Get commit hash
            hash_result = subprocess.run(
                "git rev-parse --short HEAD",
                cwd=REPO_PATH,
                capture_output=True,
                text=True,
                shell=True,
            )
            commit_hash = hash_result.stdout.strip()
            print(f"  Commit: {commit_hash}")

            return True
        else:
            print(f"{Colors.WARNING}⚠ Nothing to commit{Colors.ENDC}")
            return False

    finally:
        msg_file.unlink(missing_ok=True)


def clear_locks():
    """Remove all agent lock files after successful consolidation."""
    locks_dir = REPO_PATH / ".locks"

    if locks_dir.exists():
        for lock_file in locks_dir.glob("*.lock"):
            try:
                lock_file.unlink()
            except Exception:
                pass

    print(f"{Colors.OKGREEN}✓ Lock files cleared{Colors.ENDC}")


# ============================================================================
# AUTO-INCREMENT
# ============================================================================


def increment_patch_version():
    """Auto-increment patch version in src/config.h"""
    try:
        content = CONFIG_H.read_text()
        match = re.search(VERSION_REGEX, content)

        if not match:
            print(
                f"{Colors.FAIL}Could not find version pattern in config.h{Colors.ENDC}"
            )
            return False

        current = match.group(1)
        major, minor, patch = map(int, current.split("."))

        # Increment patch
        new_patch = patch + 1
        new_version = f"{major}.{minor}.{new_patch}"

        # Replace in config.h
        new_content = re.sub(
            VERSION_REGEX, f'#define FIRMWARE_VERSION "v{new_version}"', content
        )

        CONFIG_H.write_text(new_content)

        print(
            f"{Colors.OKGREEN}✓ Version auto-incremented: v{current} → v{new_version}{Colors.ENDC}"
        )

        # Stage the change
        subprocess.run(f"git add src/config.h", cwd=REPO_PATH, shell=True)

        return True

    except Exception as e:
        print(f"{Colors.FAIL}Error incrementing version: {e}{Colors.ENDC}")
        return False


# ============================================================================
# PUSH TO GITHUB
# ============================================================================


def push_to_github():
    """Push consolidation commit to GitHub."""
    print("\nPushing to GitHub...")

    # Get current branch
    result = subprocess.run(
        "git rev-parse --abbrev-ref HEAD",
        cwd=REPO_PATH,
        capture_output=True,
        text=True,
        shell=True,
    )

    branch = result.stdout.strip()

    if not branch:
        print(f"{Colors.FAIL}Could not determine current branch{Colors.ENDC}")
        return False

    # Push
    result = subprocess.run(
        f"git push origin {branch}",
        cwd=REPO_PATH,
        capture_output=True,
        text=True,
        shell=True,
    )

    if result.returncode == 0:
        print(f"{Colors.OKGREEN}✓ Pushed to GitHub ({branch}){Colors.ENDC}")
        return True
    else:
        print(f"{Colors.FAIL}✗ Push failed: {result.stderr}{Colors.ENDC}")
        return False


# ============================================================================
# MAIN
# ============================================================================


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    command = sys.argv[1].lower()

    current_version = get_version_from_config()
    previous_version = get_version_from_git()

    print()
    print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}")
    print(f"{Colors.BOLD}MERGE-TO-GITHUB{Colors.ENDC}")
    print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}\n")

    print(f"Current version (config.h): {current_version}")
    print(f"Previous version (git):     {previous_version}")
    print()

    if command == "detect":
        if detect_version_change():
            sys.exit(0)  # Version changed
        else:
            sys.exit(1)  # No change

    elif command == "consolidate":
        if not detect_version_change():
            print(
                f"\n{Colors.WARNING}⚠ No version change. Consolidate anyway? (y/n): {Colors.ENDC}",
                end="",
            )
            if input().lower() not in ["y", "yes"]:
                print("Cancelled")
                sys.exit(1)

        if create_consolidation_commit(current_version, previous_version):
            clear_locks()
            print()
            print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}")
            print(f"{Colors.OKGREEN}✓ Consolidation complete{Colors.ENDC}")
            print(f"{Colors.BOLD}{'=' * 70}{Colors.ENDC}\n")

    elif command == "--auto-push":
        if detect_version_change():
            if create_consolidation_commit(current_version, previous_version):
                clear_locks()
                push_to_github()

    elif command == "--auto-increment":
        if increment_patch_version():
            # Now consolidate with new version
            new_version = get_version_from_config()
            if create_consolidation_commit(new_version, current_version):
                clear_locks()

    else:
        print(f"Unknown command: {command}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.WARNING}⊗ Cancelled{Colors.ENDC}")
        sys.exit(1)
    except Exception as e:
        print(f"{Colors.FAIL}✗ Error: {e}{Colors.ENDC}")
        sys.exit(1)
