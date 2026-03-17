import os
import subprocess
import time
import urllib.request
import urllib.error
from pathlib import Path

# Required Models
MODELS = [
    "qwen2.5-coder:14b",
    "gemma3:12b", # Ensure valid string / local availability if pre-release
    "moondream"
]

def print_step(msg):
    print(f"\n[{'*' * 20}]")
    print(f"➜ {msg}")
    print(f"[{'*' * 20}]\n")

def run_command(cmd, check=True):
    try:
        subprocess.run(cmd, shell=True, check=check)
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {cmd}")
        print(e)
        if check:
            exit(1)

def verify_gpu():
    print_step("Verifying GPU Acceleration / VRAM Usage")
    run_command("nvidia-smi")

def check_ollama_running():
    print_step("Checking if Ollama runtime is active...")
    max_retries = 3
    for i in range(max_retries):
        try:
            req = urllib.request.urlopen("http://localhost:11434/")
            if req.getcode() == 200:
                print("Ollama is successfully running!")
                return True
        except urllib.error.URLError:
            pass
        print("Waiting for Ollama to become available...")
        time.sleep(2)
    
    print("Warning: Ollama is not responding at http://localhost:11434. Please ensure Ollama is installed and running.")
    print("Download from: https://ollama.com/download/windows")
    exit(1)

def pull_models():
    print_step("Pulling LLM & Vision Models")
    for model in MODELS:
        print(f"\n--- Fetching {model} ---")
        run_command(f"ollama pull {model}")
        verify_gpu()

def setup_docker_environment():
    print_step("Setting up Docker Environment (Open WebUI & Agent Sandbox)")
    
    # 1. Start Open WebUI 
    print("Starting Open WebUI container...")
    run_command("docker stop open-webui", check=False)
    run_command("docker rm open-webui", check=False)
    
    # Needs to run in docker and connect to host network for Ollama using host.docker.internal
    # Map port 3000 -> 8080
    docker_run_cmd = (
        "docker run -d -p 3000:8080 "
        "--add-host=host.docker.internal:host-gateway "
        "-v open-webui:/app/backend/data "
        "--name open-webui "
        "--restart always "
        "ghcr.io/open-webui/open-webui:main"
    )
    run_command(docker_run_cmd)
    
    # 2. Setup Agent Sandbox folder & container
    sandbox_dir = Path("agent_sandbox").resolve()
    sandbox_dir.mkdir(exist_ok=True)
    print(f"\nAgent sandbox directory created at: {sandbox_dir}")
    
    # Prepare a sleeping container for agents to run isolated scripts on
    print("Preparing secure agent sandbox execution container (Python 3.11)...")
    run_command("docker stop ai_sandbox", check=False)
    run_command("docker rm ai_sandbox", check=False)
    run_command(f"docker run -d --name ai_sandbox -v \"{sandbox_dir}:/sandbox\" -w /sandbox python:3.11-slim sleep infinity")
    
    print("\nSandbox configured. Agents can execute bash/python scripts safely using:")
    print("  `docker exec ai_sandbox python script.py`")

def apply_system_env():
    print_step("Ensuring OLLAMA_HOST is set globally for Cross-Container API access")
    # Tell Windows to bind Ollama to all interfaces so Docker can reach it.
    run_command('setx OLLAMA_HOST "0.0.0.0"')
    print("\nNOTE: You may need to restart the Ollama service to apply the 0.0.0.0 binding.")

if __name__ == "__main__":
    print_step("Local AI Workstation Configuration Tool\n(GPU: RTX 3060 12GB)")
    apply_system_env()
    check_ollama_running()
    pull_models()
    setup_docker_environment()
    print_step("Setup Complete! Navigate to http://localhost:3000 to access Open WebUI.")
