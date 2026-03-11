#!/usr/bin/env python3
"""
Upload botanical PDFs to Dify Knowledge Base
Automates document ingestion for BOTANICAL domain
"""

import requests
import sys
import time
from pathlib import Path

# Configuration
SOURCES_DIR = Path(r"C:\Users\spw1\OneDrive\Documents\Garden\sources")
DIFY_API_BASE = "http://localhost:5001/v1"
DATASET_ID = None  # We'll get this from user or from Dify

class BotanicalUploader:
    def __init__(self):
        self.session = requests.Session()
        self.dify_api = DIFY_API_BASE

    def step(self, msg):
        print(f"\n[UPLOAD] {msg}")

    def success(self, msg):
        print(f"  ✅ {msg}")

    def error(self, msg):
        print(f"  ❌ {msg}")

    def warn(self, msg):
        print(f"  ⚠️  {msg}")

    def get_dataset_id(self):
        """Get BOTANICAL dataset ID from user"""
        global DATASET_ID
        print("""
╔════════════════════════════════════════════╗
║   Botanical Knowledge Base Upload          ║
║   Uploading PDFs to Dify                   ║
╚════════════════════════════════════════════╝
        """)

        self.step("Need your BOTANICAL knowledge base Dataset ID")
        print("""
        To find it:
        1. Go to http://localhost
        2. Login: admin@dify.ai / dify-ai
        3. Click "Knowledge Base"
        4. Click on "BOTANICAL" knowledge base
        5. Copy the Dataset ID (looks like "dataset-xxxxx")
        """)

        DATASET_ID = input("\nEnter BOTANICAL Dataset ID: ").strip()
        if not DATASET_ID:
            self.error("No dataset ID provided")
            return False

        self.success(f"Using dataset: {DATASET_ID}")
        return True

    def check_health(self):
        """Verify Dify is running"""
        self.step("Checking Dify API...")
        try:
            resp = requests.get(f"{self.dify_api}/models", timeout=5)
            if resp.status_code == 200:
                self.success("Dify API is responding")
                return True
        except Exception as e:
            self.error(f"Cannot reach Dify: {e}")
            return False

    def get_file_type(self, filepath):
        """Determine file type for Dify"""
        ext = filepath.suffix.lower()
        if ext == ".pdf":
            return "pdf"
        elif ext in [".txt", ".md"]:
            return "text"
        elif ext in [".doc", ".docx"]:
            return "document"
        return "file"

    def upload_document(self, filepath):
        """Upload a single document to Dify"""
        filename = filepath.name
        file_type = self.get_file_type(filepath)

        print(f"\n  Uploading: {filename} ({filepath.stat().st_size / 1024 / 1024:.1f} MB)...")

        try:
            # Dify expects multipart form upload
            with open(filepath, 'rb') as f:
                files = {
                    'file': (filename, f, 'application/pdf' if file_type == 'pdf' else 'application/octet-stream')
                }
                data = {
                    'indexing_technique': 'high_quality',
                }

                # Upload endpoint: POST /datasets/{dataset_id}/documents
                url = f"{self.dify_api}/datasets/{DATASET_ID}/documents"

                resp = requests.post(url, files=files, data=data, timeout=60)

                if resp.status_code in [200, 201]:
                    result = resp.json()
                    doc_id = result.get('id', result.get('document_id', 'unknown'))
                    self.success(f"{filename} → Document ID: {doc_id}")
                    return True
                else:
                    self.error(f"{filename} → HTTP {resp.status_code}")
                    print(f"    Response: {resp.text[:200]}")
                    return False

        except Exception as e:
            self.error(f"{filename} → {e}")
            return False

    def upload_all(self):
        """Upload all botanical PDFs"""
        if not SOURCES_DIR.exists():
            self.error(f"Sources directory not found: {SOURCES_DIR}")
            return False

        pdfs = list(SOURCES_DIR.glob("*.pdf"))
        if not pdfs:
            self.error("No PDF files found")
            return False

        self.step(f"Found {len(pdfs)} PDF files")

        successful = 0
        failed = 0

        for i, pdf in enumerate(pdfs, 1):
            print(f"\n[{i}/{len(pdfs)}]", end=" ")
            if self.upload_document(pdf):
                successful += 1
                time.sleep(1)  # Small delay between uploads
            else:
                failed += 1

        print(f"""
╔════════════════════════════════════════════╗
║   Upload Summary                           ║
╠════════════════════════════════════════════╣
║  ✅ Successful: {successful}
║  ❌ Failed:     {failed}
║  📊 Total:      {len(pdfs)}
╚════════════════════════════════════════════╝
        """)

        if successful > 0:
            self.step("Waiting for Dify to index documents...")
            print("  (This may take a few minutes depending on file sizes)")
            print("  You can monitor progress at: http://localhost")
            return True

        return False

    def run(self):
        """Execute upload"""
        if not self.check_health():
            return False

        if not self.get_dataset_id():
            return False

        if not self.upload_all():
            return False

        print("""
Next Steps:

1. Wait for indexing to complete
   - Go to http://localhost
   - Click "Knowledge Base" → "BOTANICAL"
   - Monitor document status

2. Test the RAG with botanical data
   - Go to http://localhost:8200/test
   - Click "DHT22 readings" preset
   - Submit query

3. All documents are now part of your BOTANICAL expert knowledge!
        """)

        return True

if __name__ == "__main__":
    uploader = BotanicalUploader()
    success = uploader.run()
    sys.exit(0 if success else 1)
