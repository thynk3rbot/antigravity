import os
import subprocess
import sys
from datetime import datetime

def run_cmd(cmd):
    print(f"➜ Running: {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"❌ Error: {result.stderr}")
        return False, result.stderr
    return True, result.stdout

def sync():
    print(f"\n--- LoRaLink Daily Sync [{datetime.now().strftime('%Y-%m-%d %H:%M')}] ---")
    
    # 1. Update Repository (DEACTIVATED FOR MANUAL CONTROL)
    print("Checking for remote updates... [SKIPPED]")
    # success, _ = run_cmd("git pull origin main")
    # if not success:
    #     print("Warning: git pull failed. Proceeding with local version...")

    # 2. Build Verification (V3 Baseline)
    print("Verifying project integrity (build check)...")
    os.chdir("firmware/v2")
    # Using python -m platformio for stability as discussed
    success, _ = run_cmd("python -m platformio run -e heltec_v3")
    if not success:
        print("❌ Build failed! Sync aborted to prevent pushing broken code.")
        sys.exit(1)
    
    # 3. Commit Daily Snapshot (DEACTIVATED FOR MANUAL CONTROL)
    print("Daily Snapshot: [SKIPPED]")
    # os.chdir("../..")
    # # Get current version from platformio.ini (optional but nice)
    # run_cmd("git add .")
    # success, _ = run_cmd('git commit -m "Daily Sync: [Robot Snapshot] ' + datetime.now().strftime('%Y-%m-%d %H:%M') + '"')
    
    # if success:
    #     print("Pushing to remote repository...")
    #     run_cmd("git push origin main")
    #     print("✅ Daily Sync Complete!")
    # else:
    #     print("ℹ️ No changes to commit.")

if __name__ == "__main__":
    sync()
