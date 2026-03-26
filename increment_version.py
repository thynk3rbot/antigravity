Import("env")
import re
import os
from pathlib import Path

# ── Version format: x.x.xxV3 or x.x.xxV4 ────────────────────────────────────
# Stored in .version at repo root, one line per platform:
#   V3=0.0.14
#   V4=0.0.14
# Built as FIRMWARE_VERSION="0.0.15V3" or "0.0.15V4"
# Increments only on upload, not on build.
# ─────────────────────────────────────────────────────────────────────────────

VERSION_FILE = Path(env.get("PROJECT_DIR")).parent.parent / ".version"
PLATFORMS = {"V3": "0.0.1", "V4": "0.0.1"}


def _load_versions():
    versions = dict(PLATFORMS)
    if VERSION_FILE.exists():
        for line in VERSION_FILE.read_text().splitlines():
            line = line.strip()
            if "=" in line:
                k, v = line.split("=", 1)
                versions[k.strip()] = v.strip()
    return versions


def _save_versions(versions):
    VERSION_FILE.write_text("\n".join(f"{k}={v}" for k, v in sorted(versions.items())) + "\n")


def _detect_platform(pioenv):
    """Return 'V4' or 'V3' based on environment name."""
    if "v4" in pioenv.lower():
        return "V4"
    if "v3" in pioenv.lower():
        return "V3"
    return None


def _increment(version_str):
    """Increment rightmost digit: '0.0.14' -> '0.0.15'"""
    parts = version_str.split(".")
    parts[-1] = str(int(parts[-1]) + 1)
    return ".".join(parts)


def increment_version(source, target, env):
    ini_path = Path(env.get("PROJECT_DIR")) / "platformio.ini"
    content = ini_path.read_text() if ini_path.exists() else ""

    auto_flag = re.search(r'VERSION_AUTO_INCREMENT\s*=\s*(true|1)', content, re.IGNORECASE)
    if not auto_flag:
        print("[VERSION] Auto-increment DISABLED. Version unchanged.")
        return

    pioenv = env.subst("$PIOENV")
    platform = _detect_platform(pioenv)
    if not platform:
        print(f"[VERSION] Unrecognised env '{pioenv}' — cannot determine V3/V4. Skipping.")
        return

    versions = _load_versions()
    old = versions.get(platform, "0.0.1")
    new = _increment(old)
    versions[platform] = new
    _save_versions(versions)

    full_version = f"{new}{platform}"
    print(f"[VERSION] {platform}: {old} -> {new}  (FIRMWARE_VERSION=\"{full_version}\")")

    # Inject into build flags at runtime
    env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{full_version}\\"')])

    # Also patch the #define in the ini so it's visible in source
    pattern = r'-D FIRMWARE_VERSION=\\"[^\\"]+\\"'
    replacement = f'-D FIRMWARE_VERSION=\\\\"{full_version}\\\\"'
    new_content = re.sub(pattern, replacement, content)
    if new_content != content:
        ini_path.write_text(new_content)


# Only runs on upload, not plain build
env.AddPreAction("upload", increment_version)
