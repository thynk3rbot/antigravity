import os
import json
from pathlib import Path

def powerload():
    print("="*60)
    print(" ANTIGRAVITY POWERLOAD: MAGIC DEVELOPMENT CONTEXT")
    print("="*60)
    
    repo_root = Path(__file__).resolve().parent.parent
    
    # 1. Hardware Assets
    print("\n[1/3] Inventorying Board Schematics...")
    board_dir = repo_root / "tools" / "webapp" / "boards"
    if board_dir.exists():
        boards = list(board_dir.glob("*.json"))
        for b in boards:
            print(f"  + Found Board Definition: {b.name}")
    
    # 2. Firmware Specs & Planning
    print("\n[2/3] Collecting Firmware Specifications...")
    plan_dir = repo_root / "01_planning"
    specs = list(plan_dir.glob("spec_*.md")) if plan_dir.exists() else []
    for s in specs:
        print(f"  + Powerloading: {s.name}")

    # 3. Hydroponic & Nutrient Expertise
    print("\n[3/4] Ingesting Hydroponic Expertise Domains...")
    ag_dir = repo_root / "tools" / "nutribuddy"
    nutrients = []
    if ag_dir.exists():
        formula_files = list(ag_dir.glob("*.json"))
        for f in formula_files:
            if "formula" in f.name or "chemical" in f.name:
                print(f"  + Ingested Ag Domain: {f.name}")
                nutrients.append(f.name)

    # 4. Business & Strategy Context
    print("\n[4/4] Aligning Business Models & Branding...")
    strategy_docs = ["MARKETING_BRIEF.md", "ROADMAP.md", "MARKETING_PLAN_PROMPT.md"]
    biz_context = []
    for doc in strategy_docs:
        if (repo_root / doc).exists():
            print(f"  + Aligned Strategy: {doc}")
            biz_context.append(doc)

    # Create a summary file for Antigravity AI to ingest
    loadout_path = repo_root / ".ai_powerload.json"
    loadout = {
        "timestamp": "2026-03-13T07:49:47",
        "domains": {
            "AgTech": nutrients,
            "Strategy": biz_context,
            "Hardware": [b.name for b in board_dir.glob("*.json")] if board_dir.exists() else []
        },
        "specs": [s.name for s in specs],
        "status": "POWERLOAD_COMPLETE"
    }
    
    with open(loadout_path, "w") as f:
        json.dump(loadout, f, indent=2)
        
    print(f"\n[SUCCESS] Powerload complete. Context written to {loadout_path}")
    print("Antigravity AI is now 'primed' for AgTech expertise and Business Strategy.")
    print("="*60)

if __name__ == "__main__":
    powerload()
