# tools/quality/pipeline.py
import os
import sys
import json
import logging
import subprocess
import time
from datetime import datetime
from pathlib import Path
import requests

# Setup logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log = logging.getLogger("quality.pipeline")

# Configuration
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434/api/generate")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "qwen2.5-coder:14b")
PROJECT_ROOT = Path(__file__).parent.parent.parent
REPORTS_DIR = PROJECT_ROOT / "reports"
KNOWLEDGE_DIR = PROJECT_ROOT / "knowledge"

class QualityPipeline:
    def __init__(self):
        REPORTS_DIR.mkdir(parents=True, exist_ok=True)
        KNOWLEDGE_DIR.mkdir(parents=True, exist_ok=True)

    def _call_ollama(self, system_prompt: str, user_content: str) -> str:
        """Helper to call local Ollama API."""
        try:
            full_prompt = f"{system_prompt}\n\nContent:\n{user_content}"
            payload = {
                "model": OLLAMA_MODEL,
                "prompt": full_prompt,
                "stream": False
            }
            response = requests.post(OLLAMA_URL, json=payload, timeout=60)
            response.raise_for_status()
            return response.json().get("response", "No response.")
        except Exception as e:
            log.error(f"Ollama call failed: {e}")
            return f"Error: {e}"

    def get_diff(self, since: str = "24h") -> str:
        """Get git diff since last review."""
        try:
            cmd = ["git", "log", f"--since=\"{since}\"", "-p"]
            result = subprocess.run(cmd, cwd=PROJECT_ROOT, capture_output=True, text=True, check=True)
            return result.stdout
        except Exception as e:
            log.error(f"Failed to get git diff: {e}")
            return ""

    def review(self, since: str = "24h"):
        """Run commit review task."""
        log.info(f"Starting commit review (since: {since})")
        diff = self.get_diff(since)
        if not diff:
            log.info("No recent commits to review.")
            return

        prompt = (
            "You are a senior code reviewer for the Magic IoT platform.\n"
            "Review these commits against standards: ESP32 safety, Python async, MQTT contract.\n"
            "For each file changed, output: GRADE (A-F), ISSUES list, and PATTERNS to preserve."
        )
        
        result = self._call_ollama(prompt, diff)
        
        report_file = REPORTS_DIR / f"{datetime.now().strftime('%Y-%m-%d')}-commit-review.md"
        with open(report_file, "w") as f:
            f.write(f"# Commit Review - {datetime.now().isoformat()}\n\n")
            f.write(result)
        
        log.info(f"Review complete. Saved to {report_file}")

    def audit(self):
        """Run safety audit task on core files."""
        log.info("Starting safety audit of core library files")
        # Collect key files
        core_paths = [
            "firmware/magic/lib/",
            "daemon/src/mx/"
        ]
        
        audit_content = ""
        for path in core_paths:
            full_path = PROJECT_ROOT / path
            if full_path.exists():
                for f in full_path.glob("**/*"):
                    if f.suffix in [".cpp", ".h", ".py"] and f.is_file():
                        audit_content += f"\n--- FILE: {f.name} ---\n"
                        with open(f, "r", errors='ignore') as src:
                            audit_content += src.read()[:2000] # Limit per file

        prompt = (
            "You are a safety auditor for the Magic IoT platform.\n"
            "Focus on: ESP32 ISR safety, FreeRTOS mutexes, and Python async blocking calls.\n"
            "Grade the current safety stance of these core files."
        )
        
        result = self._call_ollama(prompt, audit_content)
        
        report_file = REPORTS_DIR / f"{datetime.now().strftime('%Y-%m-%d')}-safety-audit.md"
        with open(report_file, "w") as f:
            f.write(f"# Safety Audit - {datetime.now().isoformat()}\n\n")
            f.write(result)
            
        log.info(f"Audit complete. Saved to {report_file}")

if __name__ == "__main__":
    pipeline = QualityPipeline()
    command = sys.argv[1] if len(sys.argv) > 1 else "all"
    
    if command == "review":
        pipeline.review()
    elif command == "audit":
        pipeline.audit()
    elif command == "all":
        pipeline.review()
        pipeline.audit()
    else:
        log.error(f"Unknown command: {command}")
