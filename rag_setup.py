#!/usr/bin/env python3
"""
RAG Router Setup Script
Initializes Dify knowledge bases and RAG Router configuration via API
No web UI needed!
"""

import requests
import json
import sys
import os
import subprocess
import time
from pathlib import Path

DIFY_API = "http://localhost:5001/v1"
DIFY_USER_API = "http://localhost/v1"

class RAGSetup:
    def __init__(self):
        self.session = requests.Session()
        self.dify_api_key = None
        self.knowledge_bases = {}

    def step(self, msg):
        print(f"\n[RAG SETUP] {msg}")

    def success(self, msg):
        print(f"  ✅ {msg}")

    def error(self, msg):
        print(f"  ❌ {msg}")

    def warn(self, msg):
        print(f"  ⚠️  {msg}")

    def input_prompt(self, msg, default=None):
        prompt = msg
        if default:
            prompt += f" [{default}]"
        prompt += ": "
        val = input(prompt).strip()
        return val if val else default

    def check_dify_health(self):
        """Verify Dify is responding"""
        self.step("Checking Dify API health...")
        try:
            resp = requests.get(f"{DIFY_API}/models", timeout=5)
            if resp.status_code == 200:
                self.success("Dify API is responding")
                return True
        except Exception as e:
            self.error(f"Cannot reach Dify API: {e}")
            self.error("Make sure docker compose is running:")
            print("  cd tools/rag_router")
            print("  docker compose up -d")
            return False

    def create_sample_knowledge_base(self, domain, description):
        """Collect knowledge base IDs from user"""
        self.step(f"Setting up {domain} knowledge base...")

        self.warn(f"Knowledge base creation requires Dify admin access")
        self.warn("Please follow these steps:")
        print(f"""
        1. Open http://localhost in your browser
        2. Login with:
           Email: admin@dify.ai
           Password: dify-ai
        3. In the left sidebar, click "Knowledge Base"
        4. Click "Create Knowledge Base" (or "New KB")
        5. Enter name: {domain}
        6. Enter description: {description}
        7. Click "Create" and wait
        8. (Optional but recommended) Upload PDF documents:
           - Click the knowledge base
           - Click "Add file" or "Upload"
           - Select PDFs about {domain}
           - Wait for "Completed" status
        9. Go back to knowledge bases list
        10. Find your {domain} knowledge base
        11. Look for "Dataset ID" or "ID" field
        12. Copy the ID (looks like "dataset-xxxx-yyyy-zzzz")
        """)

        dataset_id = self.input_prompt(f"\nEnter the {domain} Dataset ID from Dify")
        if not dataset_id:
            self.warn(f"Skipping {domain}")
            return None

        self.success(f"{domain} dataset ID: {dataset_id}")
        return dataset_id

    def update_env_file(self):
        """Update .env with knowledge base IDs"""
        # Change to repo root
        repo_root = Path(__file__).parent.parent.parent / "OneDrive" / "Documents" / "Code" / "Antigravity Repository" / "antigravity"
        os.chdir(repo_root)

        env_path = Path("tools/rag_router/.env")

        if not env_path.exists():
            self.step("Creating .env file from .env.example...")
            example_path = Path("tools/rag_router/.env.example")
            if example_path.exists():
                env_path.write_text(example_path.read_text())
                self.success(".env created")
            else:
                self.error(".env.example not found")
                return False

        self.step("Updating .env with knowledge base IDs...")

        content = env_path.read_text()

        # Collect knowledge base IDs
        datasets = {}
        domains = {
            "NUTRIENT": "Hydroponic nutrient management, pH/EC optimization",
            "BOTANICAL": "Plant growth, environmental conditions, VPD",
            "HARDWARE": "Device troubleshooting, relay/sensor diagnostics"
        }

        for domain, desc in domains.items():
            dataset_id = self.create_sample_knowledge_base(domain, desc)
            if dataset_id:
                datasets[domain] = dataset_id

        # Update env file
        for domain, dataset_id in datasets.items():
            key = f"KNOWLEDGE_ID_{domain}"
            if key in content:
                # Replace existing
                lines = []
                for line in content.split('\n'):
                    if line.startswith(key + '='):
                        lines.append(f"{key}={dataset_id}")
                    else:
                        lines.append(line)
                content = '\n'.join(lines)
            else:
                # Append
                content += f"\n{key}={dataset_id}"

        env_path.write_text(content)
        self.success(f"Updated .env with {len(datasets)} knowledge bases")
        return True

    def restart_rag_router(self):
        """Restart RAG Router to load new config"""
        self.step("Restarting RAG Router...")
        try:
            subprocess.run(["docker", "compose", "restart", "rag-router"],
                          cwd="tools/rag_router", check=True, capture_output=True)
            self.success("RAG Router restarted")
        except subprocess.CalledProcessError as e:
            self.error(f"Failed to restart: {e}")

    def test_pipeline(self):
        """Test the RAG pipeline with sample data"""
        self.step("Testing RAG pipeline...")

        test_data = {
            "id": "TEST-01",
            "hwType": "ph-ec-sensor",
            "data": {
                "ph": 6.1,
                "ec": 1.8,
                "temp": 22.5
            }
        }

        try:
            resp = requests.post(
                "http://localhost:8200/api/ingest-and-query",
                json=test_data,
                timeout=10
            )

            if resp.status_code == 200:
                result = resp.json()
                if result.get("guard_rejected"):
                    self.warn("Guard rejected test: check knowledge base setup")
                elif result.get("error"):
                    self.error(f"Error: {result['error']}")
                else:
                    answer = result.get('answer', '')[:100]
                    self.success(f"RAG Response: {answer}...")
            else:
                self.error(f"HTTP {resp.status_code}: {resp.text[:200]}")
        except Exception as e:
            self.error(f"Test failed: {e}")

    def run(self):
        """Execute setup"""
        print("""
╔═══════════════════════════════════════════╗
║   RAG Router Setup & Initialization       ║
║   Universal IoT-to-RAG Configuration      ║
╚═══════════════════════════════════════════╝
        """)

        # Step 1: Health check
        if not self.check_dify_health():
            return False

        # Step 2: Create knowledge bases
        if not self.update_env_file():
            return False

        # Step 3: Restart RAG Router
        self.restart_rag_router()

        # Step 4: Test
        print("\nWaiting 3 seconds for RAG Router restart...")
        time.sleep(3)
        self.test_pipeline()

        # Final instructions
        print("""
╔═══════════════════════════════════════════╗
║   Setup Complete! 🎉                      ║
╚═══════════════════════════════════════════╝

Next Steps:

1. Upload Documents to Your Knowledge Bases
   - Go to http://localhost
   - Login: admin@dify.ai / dify-ai
   - For each knowledge base (NUTRIENT, BOTANICAL, HARDWARE):
     a. Click the knowledge base
     b. Click "Upload document" or "Add file"
     c. Select PDFs or text files
     d. Wait for "Completed" status

2. Test the RAG Router
   - http://localhost:8200/test     (test form)
   - http://localhost:8200          (full console)
   - Try the "pH+EC OK" test preset

3. Connect Your Data Source
   - MQTT: publish to loralink/<nodeId>/sensor/<key>
   - HTTP: POST to http://localhost:8200/api/ingest-and-query
   - WebSocket: connect to ws://localhost:8200/ws

Your RAG Router is now ready to process any IoT data! 🚀
        """)

        return True

if __name__ == "__main__":
    setup = RAGSetup()
    success = setup.run()
    sys.exit(0 if success else 1)
