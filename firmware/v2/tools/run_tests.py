#!/usr/bin/env python3
"""LoRaLink v2 test runner — runs native unit tests + integration checks."""
import subprocess, sys, time

BENCH_IP = "172.16.0.26"

def run(name, fn):
    try:
        fn()
        print(f"  PASS  {name}")
        return True
    except Exception as e:
        print(f"  FAIL  {name}: {e}")
        return False

def test_native_units():
    r = subprocess.run(
        ["python", "-m", "platformio", "test", "--environment", "native"],
        capture_output=True, text=True,
        cwd="C:\\Users\\spw1\\Documents\\Code\\Antigravity\\firmware\\v2"
    )
    if r.returncode != 0:
        raise Exception(r.stdout[-800:] + r.stderr[-400:])

def test_build_v3():
    r = subprocess.run(
        ["python", "-m", "platformio", "run", "--environment", "heltec_v3_node"],
        capture_output=True, text=True,
        cwd="C:\\Users\\spw1\\Documents\\Code\\Antigravity\\firmware\\v2"
    )
    if r.returncode != 0:
        raise Exception(r.stderr[-500:])

def test_http_status():
    import urllib.request, json
    with urllib.request.urlopen(f"http://{BENCH_IP}/api/status", timeout=6) as r:
        d = json.loads(r.read())
    assert "id" in d and "version" in d, f"Bad response: {d}"
    print(f"         -> {d.get('id')} {d.get('version')}")

if __name__ == "__main__":
    print("=== LoRaLink v2 Test Suite ===\n")
    tests = [
        ("native unit tests", test_native_units),
        ("build heltec_v3_node", test_build_v3),
        ("HTTP /api/status (bench)", test_http_status),
    ]
    results = [run(name, fn) for name, fn in tests]
    passed = sum(results)
    print(f"\n{passed}/{len(results)} passed")
    sys.exit(0 if all(results) else 1)
