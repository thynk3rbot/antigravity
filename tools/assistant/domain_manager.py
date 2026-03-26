import sys
import json
import logging
from pathlib import Path
from typing import List, Dict, Optional

# Add framework to path to reuse Orion components
FRAMEWORK_PATH = Path(__file__).parent.parent / "multi-agent-framework"
if str(FRAMEWORK_PATH) not in sys.path:
    sys.path.append(str(FRAMEWORK_PATH))

try:
    from rag.retriever import Retriever
except ImportError:
    # Handle case where rag is not a package or structure is different
    # If the framework is not structured as a package, we might need more path hacks
    logger = logging.getLogger("DomainManager")
    logger.error(f"Failed to import RAG components from {FRAMEWORK_PATH}")

logger = logging.getLogger("DomainManager")

class DomainConfig:
    def __init__(self, data: dict):
        self.id = data["id"]
        self.name = data["name"]
        self.description = data.get("description", "")
        self.sources = data.get("sources", [])
        self.collection_name = data.get("collection_name", f"domain_{self.id}")
        self.chunk_size = data.get("chunk_size", 1024)
        self.chunk_overlap = data.get("chunk_overlap", 100)
        self.top_k = data.get("top_k", 5)
        self.system_prompt = data.get("system_prompt", "You are a helpful AI assistant.")

class DomainManager:
    def __init__(self, domains_dir: Path, persist_dir: Path, global_config: dict):
        self.domains_dir = domains_dir
        self.persist_dir = persist_dir
        self.global_config = global_config
        self._domains: Dict[str, DomainConfig] = {}
        self._retrievers: Dict[str, Retriever] = {}
        self.domains_dir.mkdir(parents=True, exist_ok=True)
        self._load_domains()

    def _load_domains(self):
        """Loads all domain JSON configs from the domains directory."""
        for p in self.domains_dir.glob("*.json"):
            try:
                with open(p, "r") as f:
                    data = json.load(f)
                    config = DomainConfig(data)
                    self._domains[config.id] = config
                    logger.info(f"Loaded domain: {config.name} ({config.id})")
            except Exception as e:
                logger.error(f"Failed to load domain config {p}: {e}")

    def list_domains(self) -> List[dict]:
        """Returns a list of all available domains."""
        return [
            {
                "id": d.id,
                "name": d.name,
                "description": d.description,
                "sources": d.sources
            }
            for d in self._domains.values()
        ]

    def get_config(self, domain_id: str) -> Optional[DomainConfig]:
        return self._domains.get(domain_id)

    async def query(self, domain_id: str, question: str) -> List[dict]:
        """Queries a specific domain's RAG collection."""
        if domain_id not in self._domains:
            logger.warning(f"Domain {domain_id} not found")
            return []

        if domain_id not in self._retrievers:
            # Create a specialized config for this retriever instance
            domain_cfg = self._domains[domain_id]
            retriever_config = self.global_config.copy()
            # Override RAG settings for this specific domain collection
            retriever_config["rag"] = {
                "enabled": True,
                "persist_directory": str(self.persist_dir),
                "collection_name": domain_cfg.collection_name,
                "top_k": domain_cfg.top_k,
                "embedding_model": self.global_config.get("rag", {}).get("embedding_model", "nomic-embed-text")
            }
            
            from rag.retriever import Retriever
            self._retrievers[domain_id] = Retriever(retriever_config)

        retriever = self._retrievers[domain_id]
        return await retriever.retrieve(question)
