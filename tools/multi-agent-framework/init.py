#!/usr/bin/env python3
"""
Multi-Agent Framework Scaffolding Tool

Creates the directory structure, coordination docs, and tool scripts
for a new multi-agent project from a config.json file.

Usage:
    python init.py --config config.json --target /path/to/new/project
    python init.py --config config.json                # scaffold in current dir
"""

import argparse
import json
import shutil
from pathlib import Path

FRAMEWORK_DIR = Path(__file__).resolve().parent
TEMPLATES_DIR = FRAMEWORK_DIR / "templates"
TOOL_FILES = ["agent_tracking.py", "hybrid_model_proxy.py", "ollama_bridge.py"]


def load_config(config_path: str) -> dict:
    with open(config_path) as f:
        return json.load(f)


def build_variables(config: dict) -> dict:
    """Extract template variables from config."""
    agents = config.get("agents", {})
    phases = config.get("phases", {})

    planner = agents.get("planner", {})
    executor = agents.get("executor", {})
    reviewer = agents.get("reviewer", {})

    p1 = phases.get("1", {})
    p2 = phases.get("2", {})
    p3 = phases.get("3", {})

    build_cmd = config.get("project", {}).get("build_command")
    build_rule = f"Must pass `{build_cmd}`. " if build_cmd else ""

    return {
        "project_name": config.get("project", {}).get("name", "MyProject"),
        "project_description": config.get("project", {}).get("description", ""),
        # Planner
        "planner_name": planner.get("name", "Planner"),
        "planner_name_lower": planner.get("name", "Planner").lower(),
        "planner_tool": planner.get("tool", "sgpt"),
        "planner_lock": planner.get("lock_file", "planner.lock"),
        "planner_dirs": ", ".join(planner.get("directories", [])),
        # Executor
        "executor_name": executor.get("name", "Executor"),
        "executor_name_lower": executor.get("name", "Executor").lower(),
        "executor_tool": executor.get("tool", "claude-code"),
        "executor_lock": executor.get("lock_file", "executor.lock"),
        "executor_dirs": ", ".join(executor.get("directories", [])),
        # Reviewer
        "reviewer_name": reviewer.get("name", "Reviewer"),
        "reviewer_name_lower": reviewer.get("name", "Reviewer").lower(),
        "reviewer_tool": reviewer.get("tool", "manual"),
        "reviewer_lock": reviewer.get("lock_file", "reviewer.lock"),
        "reviewer_dirs": ", ".join(reviewer.get("directories", [])),
        # Phases
        "phase_1_dir": p1.get("directory", "01_planning/").rstrip("/"),
        "phase_1_input": p1.get("input", "idea.txt"),
        "phase_1_output": p1.get("output", "spec.md"),
        "phase_2_dir": p2.get("directory", "02_coding/").rstrip("/"),
        "phase_3_dir": p3.get("directory", "03_review/").rstrip("/"),
        "phase_3_output": p3.get("output", "audit_report.md"),
        # Build
        "build_verify_rule": build_rule,
    }


def render_template(template_path: Path, variables: dict) -> str:
    """Replace {{key}} placeholders in a template file."""
    text = template_path.read_text(encoding="utf-8")
    for key, value in variables.items():
        text = text.replace("{{" + key + "}}", str(value))
    return text


def scaffold(config: dict, target_dir: Path):
    """Create the full project structure."""
    target_dir.mkdir(parents=True, exist_ok=True)
    variables = build_variables(config)
    phases = config.get("phases", {})
    locks_dir_name = config.get("lock_settings", {}).get("locks_directory", ".locks")

    # 1. Create phase directories
    print(f"\nScaffolding project: {variables['project_name']}")
    print(f"Target: {target_dir}\n")

    for phase_num, phase_cfg in phases.items():
        phase_dir = target_dir / phase_cfg.get("directory", f"0{phase_num}_phase{phase_num}/").rstrip("/")
        phase_dir.mkdir(parents=True, exist_ok=True)
        print(f"  + {phase_dir.name}/")

    # 2. Create locks directory
    locks_dir = target_dir / locks_dir_name
    locks_dir.mkdir(parents=True, exist_ok=True)
    print(f"  + {locks_dir_name}/")

    # 3. Create tools directory and copy framework tools
    tools_dir = target_dir / "tools"
    tools_dir.mkdir(parents=True, exist_ok=True)
    print(f"  + tools/")

    for tool_file in TOOL_FILES:
        src = FRAMEWORK_DIR / tool_file
        if src.exists():
            shutil.copy2(src, tools_dir / tool_file)
            print(f"    + {tool_file}")

    # Copy RAG modules
    rag_src = FRAMEWORK_DIR / "rag"
    rag_dst = tools_dir / "rag"
    if rag_src.exists():
        shutil.copytree(rag_src, rag_dst, dirs_exist_ok=True)
        print(f"    + rag/")

    # 4. Copy config
    config_dst = target_dir / "config.json"
    with open(config_dst, "w") as f:
        json.dump(config, f, indent=2)
    print(f"  + config.json")

    # 5. Render and write template docs
    docs_dir = target_dir / "docs"
    docs_dir.mkdir(parents=True, exist_ok=True)
    print(f"  + docs/")

    for template_file in TEMPLATES_DIR.glob("*.md"):
        rendered = render_template(template_file, variables)
        out_path = docs_dir / template_file.name
        out_path.write_text(rendered, encoding="utf-8")
        print(f"    + {template_file.name}")

    # 6. Create .gitignore entries
    gitignore_path = target_dir / ".gitignore"
    gitignore_entries = [
        f"# Multi-Agent Framework",
        f"{locks_dir_name}/",
        f".rag_store/",
        f"*.lock",
        f"__pycache__/",
    ]
    if gitignore_path.exists():
        existing = gitignore_path.read_text()
        new_entries = [e for e in gitignore_entries if e not in existing]
        if new_entries:
            with open(gitignore_path, "a") as f:
                f.write("\n" + "\n".join(new_entries) + "\n")
            print(f"  ~ .gitignore (appended)")
    else:
        gitignore_path.write_text("\n".join(gitignore_entries) + "\n")
        print(f"  + .gitignore")

    # 7. Create requirements.txt
    req_path = tools_dir / "requirements.txt"
    req_path.write_text("httpx>=0.27.0\nchromadb>=0.4.0\n# PyMuPDF>=1.24.0  # optional, for PDF ingestion\n")
    print(f"    + requirements.txt")

    print(f"\nDone! Project scaffolded at {target_dir}")
    print(f"\nNext steps:")
    print(f"  1. cd {target_dir}")
    print(f"  2. pip install -r tools/requirements.txt")
    print(f"  3. Review docs/ for agent coordination guides")
    print(f"  4. Start working: python tools/agent_tracking.py acquire <YourAgent> \"First task\"")
    if config.get("rag", {}).get("enabled"):
        print(f"  5. Ingest domain docs: python -m tools.rag.ingest --source docs/")


def main():
    parser = argparse.ArgumentParser(description="Scaffold a multi-agent project")
    parser.add_argument("--config", required=True, help="Path to config.json")
    parser.add_argument("--target", default=".", help="Target directory (default: current)")
    args = parser.parse_args()

    config = load_config(args.config)
    scaffold(config, Path(args.target).resolve())


if __name__ == "__main__":
    main()
