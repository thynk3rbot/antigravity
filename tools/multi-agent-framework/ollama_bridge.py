#!/usr/bin/env python3
"""
Ollama Bridge — Cross-platform task delegation to local Ollama models.

Replaces the Windows-only .bat bridge with a portable Python script.
Supports task types: search-replace, generate-code, analyze, find-files, refactor.

Usage:
    python ollama_bridge.py "generate-code" "Write a function to validate GPIO pins"
    python ollama_bridge.py --config config.json "analyze" "Review this code for bugs"
    python ollama_bridge.py --list-types
"""

import argparse
import json
import sys
from pathlib import Path

import httpx

DEFAULT_MODEL = "qwen2.5-coder:14b"
DEFAULT_URL = "http://localhost:11434"

TASK_PROMPTS = {
    "search-replace": "You are a code assistant. Perform the following search-and-replace task. Output ONLY the changed code, no explanation.",
    "generate-code": "You are a code assistant. Generate the following code. Output ONLY the code, no explanation or preamble.",
    "analyze": "You are a code reviewer. Analyze the following and report issues, bugs, or improvements. Be concise.",
    "find-files": "You are a codebase navigator. Help locate files matching the following criteria. List file paths only.",
    "refactor": "You are a refactoring expert. Suggest refactoring for the following. Show the refactored code.",
}


def load_config(config_path):
    if config_path and Path(config_path).exists():
        with open(config_path) as f:
            return json.load(f)
    cwd_config = Path.cwd() / "config.json"
    if cwd_config.exists():
        with open(cwd_config) as f:
            return json.load(f)
    return {}


def run_task(task_type, description, model, base_url):
    system_prompt = TASK_PROMPTS.get(task_type, TASK_PROMPTS["analyze"])
    full_prompt = f"{system_prompt}\n\nTask: {description}"

    print(f"\n{'=' * 60}")
    print(f"Ollama Bridge")
    print(f"  Task: {task_type}")
    print(f"  Model: {model}")
    print(f"{'=' * 60}\n")

    try:
        with httpx.Client(timeout=600) as client:
            resp = client.post(
                f"{base_url}/api/generate",
                json={"model": model, "prompt": full_prompt, "stream": False},
            )
            resp.raise_for_status()
            result = resp.json()
            print(result.get("response", "(no response)"))
            print(f"\n{'=' * 60}")
            tokens = result.get("prompt_eval_count", 0) + result.get("eval_count", 0)
            print(f"Tokens: {tokens} | Cost: $0.00 (local)")
            print(f"{'=' * 60}\n")
    except httpx.ConnectError:
        print(f"ERROR: Cannot connect to Ollama at {base_url}")
        print("Is Ollama running? Start it with: ollama serve")
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Ollama Bridge — delegate tasks to local models")
    parser.add_argument("task_type", nargs="?", help="Task type (search-replace, generate-code, analyze, find-files, refactor)")
    parser.add_argument("description", nargs="?", help="Task description")
    parser.add_argument("--config", help="Path to config.json")
    parser.add_argument("--model", help="Override model name")
    parser.add_argument("--list-types", action="store_true", help="List available task types")
    args = parser.parse_args()

    if args.list_types:
        print("Available task types:")
        for name, prompt in TASK_PROMPTS.items():
            print(f"  {name:20s} — {prompt[:60]}...")
        return

    if not args.task_type or not args.description:
        parser.print_help()
        sys.exit(1)

    config = load_config(args.config)
    proxy_cfg = config.get("model_proxy", {})
    model = args.model or proxy_cfg.get("local", {}).get("default_model", DEFAULT_MODEL)
    base_url = proxy_cfg.get("local", {}).get("base_url", DEFAULT_URL)

    run_task(args.task_type, args.description, model, base_url)


if __name__ == "__main__":
    main()
