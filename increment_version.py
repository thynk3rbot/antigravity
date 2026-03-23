Import("env")
import re
import os

def increment_version(source, target, env):
    ini_path = os.path.join(env.get("PROJECT_DIR"), "platformio.ini")
    if not os.path.exists(ini_path):
        print(f"Error: {ini_path} not found")
        return

    with open(ini_path, "r") as f:
        content = f.read()

    # Match -D FIRMWARE_VERSION=\"x.y.z\"
    pattern = r'(-D FIRMWARE_VERSION=\\?\"v?)(\d+\.\d+\.)(\d+)(\\?\")'
    match = re.search(pattern, content)

    if match:
        prefix = match.group(1)
        base = match.group(2)
        patch = int(match.group(3))
        suffix = match.group(4)

        new_patch = patch + 1
        new_version = f"{prefix}{base}{new_patch}{suffix}"

        new_content = re.sub(pattern, new_version, content)

        with open(ini_path, "w") as f:
            f.write(new_content)

        print(f"Auto-incremented version to: {base}{new_patch}")
    else:
        print("Warning: Could not find FIRMWARE_VERSION pattern in platformio.ini")

# Register to run before upload
env.AddPreAction("upload", increment_version)
