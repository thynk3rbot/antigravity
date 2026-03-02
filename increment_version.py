Import("env")
import re
import os


def increment_version(source, target, env):
    config_path = os.path.join(env.get("PROJECT_DIR"), "src", "config.h")
    if not os.path.exists(config_path):
        print(f"Error: {config_path} not found")
        return

    with open(config_path, "r") as f:
        content = f.read()

    # Match #define FIRMWARE_VERSION "vX.Y.Z"
    pattern = r'(#define FIRMWARE_VERSION\s+"v)(\d+\.\d+\.)(\d+)(")'
    match = re.search(pattern, content)

    if match:
        prefix = match.group(1)
        base = match.group(2)
        patch = int(match.group(3))
        suffix = match.group(4)

        new_patch = patch + 1
        new_version = f"{prefix}{base}{new_patch}{suffix}"

        new_content = re.sub(pattern, new_version, content)

        with open(config_path, "w") as f:
            f.write(new_content)

        print(f"Auto-incremented version to: {base}{new_patch}")
    else:
        print("Warning: Could not find FIRMWARE_VERSION pattern to increment")


# Register the increment function to run specifically during the upload process
env.AddPreAction("upload", increment_version)
