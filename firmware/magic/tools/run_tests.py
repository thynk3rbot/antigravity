#!/usr/bin/env python3
"""Magic v2 test runner — runs native unit tests + integration checks."""
import subprocess, sys, time, os, json, urllib.request

# Configuration with environment variable overrides
REPO_ROOT = os.environ.get(
    "MAGIC_REPO",
    "C:\\Users\\spw1\\Documents\\Code\\Antigravity\\firmware\\v2"
)
BENCH_IP = os.environ.get("BENCH_IP", "172.16.0.26")
BENCH_PORT = os.environ.get("BENCH_PORT", "80")
BENCH_TIMEOUT = int(os.environ.get("BENCH_TIMEOUT", "6"))

def run(description, fn):
    print(f"  {description}...", end=" ", flush=True)
    try:
        fn()
        print("✓")
        return True
    except Exception as e:
        print(f"✗\n    {e}")
        return False

def test_native_units():
    r = subprocess.run(
        ["python", "-m", "platformio", "test", "--environment", "native"],
        capture_output=True, text=True,
        cwd=REPO_ROOT
    )
    if r.returncode != 0:
        raise Exception(r.stdout[-800:] + r.stderr[-400:])

def test_build_v3():
    r = subprocess.run(
        ["python", "-m", "platformio", "run", "--environment", "heltec_v3_node"],
        capture_output=True, text=True,
        cwd=REPO_ROOT
    )
    if r.returncode != 0:
        raise Exception(r.stderr[-500:])

def test_http_status():
    """Test HTTP /api/status endpoint with 3 retries."""
    max_retries = 3
    retry_delay = 1.0

    for attempt in range(max_retries):
        try:
            with urllib.request.urlopen(
                f"http://{BENCH_IP}:{BENCH_PORT}/api/status",
                timeout=BENCH_TIMEOUT
            ) as r:
                d = json.loads(r.read())
            assert "id" in d and "version" in d, f"Bad response: {d}"
            return  # Success
        except Exception as e:
            if attempt < max_retries - 1:
                time.sleep(retry_delay)
            else:
                raise  # All retries exhausted

if __name__ == "__main__":
    print("=== Magic v2 Test Suite ===\n")
    print(f"Using repository root: {REPO_ROOT}")
    print(f"Bench IP: {BENCH_IP}:{BENCH_PORT}, timeout: {BENCH_TIMEOUT}s\n")

    tests = [
        ("native unit tests", test_native_units),
        ("build heltec_v3_node", test_build_v3),
        ("HTTP /api/status (bench)", test_http_status),
    ]
    results = [run(description, fn) for description, fn in tests]
    passed = sum(results)
    print(f"\n{passed}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)
