"""
Community — peer daemon registry and health polling.

Daemons discover each other via daemon/config.json:
  "community": [
    {"name": "Shop", "url": "http://192.168.1.10:8001", "description": "Workshop fleet"},
    {"name": "Field", "url": "http://10.0.0.5:8001",   "description": "Field deployment"}
  ]

Each peer is polled every 30s for health + fleet status.
The webapp shows all peers in the Ops Center — one surface for the whole community.
"""

import asyncio
import json
import logging
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import httpx

logger = logging.getLogger(__name__)

POLL_INTERVAL = 30  # seconds


@dataclass
class PeerDaemon:
    name: str
    url: str
    description: str = ""
    online: bool = False
    last_seen: Optional[float] = None
    peers_total: int = 0
    peers_online: int = 0
    services_running: List[str] = field(default_factory=list)
    error: Optional[str] = None

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "url": self.url,
            "description": self.description,
            "online": self.online,
            "last_seen": self.last_seen,
            "peers_total": self.peers_total,
            "peers_online": self.peers_online,
            "services_running": self.services_running,
            "error": self.error,
            "age_s": int(time.time() - self.last_seen) if self.last_seen else None,
        }


class CommunityManager:
    def __init__(self, config_path: Path):
        self._config_path = config_path
        self._peers: Dict[str, PeerDaemon] = {}
        self._task: Optional[asyncio.Task] = None
        self._reload()

    def _reload(self):
        """Load peer list from config.json."""
        if not self._config_path.exists():
            return
        try:
            cfg = json.loads(self._config_path.read_text())
            community = cfg.get("community", [])
            seen = set()
            for entry in community:
                name = entry["name"]
                seen.add(name)
                if name not in self._peers:
                    self._peers[name] = PeerDaemon(
                        name=name,
                        url=entry["url"].rstrip("/"),
                        description=entry.get("description", ""),
                    )
                else:
                    # Update URL/description if config changed
                    self._peers[name].url = entry["url"].rstrip("/")
                    self._peers[name].description = entry.get("description", "")
            # Remove peers that were removed from config
            for name in list(self._peers.keys()):
                if name not in seen:
                    del self._peers[name]
        except Exception as e:
            logger.warning(f"[Community] Failed to load peers: {e}")

    def reload(self):
        """Call after config.json is saved."""
        self._reload()

    def start(self):
        """Start background polling loop."""
        if self._task is None or self._task.done():
            self._task = asyncio.create_task(self._poll_loop())

    def stop(self):
        if self._task:
            self._task.cancel()

    async def _poll_loop(self):
        while True:
            await self._poll_all()
            await asyncio.sleep(POLL_INTERVAL)

    async def _poll_all(self):
        tasks = [self._poll_peer(p) for p in self._peers.values()]
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    async def _poll_peer(self, peer: PeerDaemon):
        try:
            async with httpx.AsyncClient(timeout=5) as client:
                r = await client.get(f"{peer.url}/health")
                if r.status_code == 200:
                    data = r.json()
                    peer.online = True
                    peer.last_seen = time.time()
                    peer.peers_total = data.get("peers", 0)
                    peer.peers_online = data.get("peers_online", peer.peers_total)
                    peer.services_running = data.get("services_running", [])
                    peer.error = None
                else:
                    peer.online = False
                    peer.error = f"HTTP {r.status_code}"
        except Exception as e:
            peer.online = False
            peer.error = str(e)[:80]

    def status_all(self) -> List[dict]:
        return [p.to_dict() for p in self._peers.values()]

    def peer_count(self) -> int:
        return len(self._peers)

    def online_count(self) -> int:
        return sum(1 for p in self._peers.values() if p.online)
