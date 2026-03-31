"""
Consistent Hash Ring: Deterministic peer routing at scale.

Problem: "Which peer should handle command for DEV042?"
Solution: All devices & daemon run same hash function → Same answer always

Benefit:
- O(log n) routing lookup
- No central registry needed (though we still use it for IP mapping)
- Scales to 10,000+ devices
- Devices can route independently

Pattern:
1. Daemon publishes ring (peer list)
2. All devices apply same hash
3. Command for DEV042 routes to peer #7 (same everywhere)
4. Peer #7 routes to DEV042 directly
"""

import hashlib
from bisect import bisect_left
from typing import List, Optional, Dict
import logging

logger = logging.getLogger(__name__)


class PeerRing:
    """Consistent hash ring for peer-to-peer routing."""

    def __init__(self, peers: List[str] = None, virtual_nodes: int = 3):
        """
        Initialize ring.

        Args:
            peers: List of peer/device IDs (e.g., ["DEV001", "DEV002"])
            virtual_nodes: Number of virtual copies per peer (for load distribution)
        """
        self.peers = peers or []
        self.virtual_nodes = virtual_nodes
        self.ring: Dict[int, str] = {}  # hash -> peer_id
        self.sorted_keys: List[int] = []
        self.rebuild()

    def rebuild(self):
        """Rebuild ring (call after adding/removing peers)."""
        self.ring = {}
        self.sorted_keys = []

        for peer in self.peers:
            for i in range(self.virtual_nodes):
                key = self._hash(f"{peer}#{i}")
                self.ring[key] = peer
                self.sorted_keys.append(key)

        self.sorted_keys.sort()
        logger.info(f"[PeerRing] Rebuilt with {len(self.peers)} peers, "
                   f"{len(self.ring)} total nodes")

    def add_peer(self, peer_id: str):
        """Add peer to ring."""
        if peer_id not in self.peers:
            self.peers.append(peer_id)
            self.rebuild()

    def remove_peer(self, peer_id: str):
        """Remove peer from ring."""
        if peer_id in self.peers:
            self.peers.remove(peer_id)
            self.rebuild()

    def get_peer(self, key: str) -> Optional[str]:
        """
        Get responsible peer for a key using consistent hashing.

        Args:
            key: Target key (e.g., device_id "DEV042")

        Returns:
            Peer ID responsible for this key (e.g., "DEV001")
        """
        if not self.sorted_keys:
            return None

        h = self._hash(key)
        idx = bisect_left(self.sorted_keys, h)

        # Wrap around
        if idx == len(self.sorted_keys):
            idx = 0

        return self.ring[self.sorted_keys[idx]]

    def get_peers(self, key: str, replicas: int = 3) -> List[str]:
        """
        Get N replicas (different peers) for a key.

        Useful for redundancy: store command result on 3 peers.

        Args:
            key: Target key
            replicas: Number of peers to return

        Returns:
            List of peer IDs (up to `replicas` count, no duplicates)
        """
        if not self.sorted_keys:
            return []

        peers_set = set()
        h = self._hash(key)
        idx = bisect_left(self.sorted_keys, h)

        while len(peers_set) < min(replicas, len(self.peers)):
            peer = self.ring[self.sorted_keys[idx % len(self.sorted_keys)]]
            peers_set.add(peer)
            idx += 1

        return list(peers_set)

    def export(self) -> Dict:
        """Export ring for device sync (wire format)."""
        return {
            "peers": self.peers,
            "virtual_nodes": self.virtual_nodes,
            "timestamp_ms": int(__import__('time').time() * 1000)
        }

    @staticmethod
    def _hash(key: str) -> int:
        """Hash string to 32-bit integer."""
        h = hashlib.md5(key.encode()).digest()
        return int.from_bytes(h[:4], byteorder='big') & 0xFFFFFFFF


# Example usage for daemon
def example_daemon():
    """Daemon side: route command to peer."""
    ring = PeerRing(peers=["DEV001", "DEV002", "DEV003", "DEV004", "DEV005"])

    # Command for DEV042
    target = ring.get_peer("DEV042")
    print(f"Route DEV042 → {target}")
    # Output: Route DEV042 → DEV003

    # Get 3 replicas for redundancy
    replicas = ring.get_peers("DEV042", replicas=3)
    print(f"Replicas for DEV042: {replicas}")
    # Output: Replicas for DEV042: ['DEV003', 'DEV001', 'DEV004']


# Example: Device side would be nearly identical (C++)
# Firmware receives: {"peers": ["DEV001", ...], "virtual_nodes": 3}
# Device applies same hash logic → same peer determination
