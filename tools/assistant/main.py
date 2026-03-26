import subprocess
import sys
import time
from pathlib import Path

def main():
    """Main entry point for the LoRaLink Assistant."""
    root = Path(__file__).parent.resolve()
    
    print("--- Starting LoRaLink Assistant ---")
    print(f"Root: {root}")

    # 1. Start Server
    # Note: We use -u for unbuffered output to see logs in real-time if needed
    server_proc = subprocess.Popen(
        [sys.executable, str(root / "server.py")],
        cwd=str(root)
    )
    print(f"Backend server started (PID: {server_proc.pid})")

    # 2. Wait a moment for server to bind
    time.sleep(2)

    # 3. Start Tray (Blocking)
    print("Starting System Tray...")
    try:
        subprocess.run(
            [sys.executable, str(root / "tray.py")],
            cwd=str(root),
            check=False
        )
    except KeyboardInterrupt:
        print("\nShutdown signal received.")
    finally:
        # 4. Cleanup
        print("Shutting down backend server...")
        server_proc.terminate()
        try:
            server_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server_proc.kill()
        print("Goodbye.")

if __name__ == "__main__":
    main()
