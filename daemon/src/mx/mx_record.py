from dataclasses import dataclass, field
from typing import Any
import time

@dataclass
class MxRecord:
    subject: str
    sequence: int = 0
    timestamp: float = field(default_factory=time.time)
    fields: dict = field(default_factory=dict)
    dirty: set = field(default_factory=set)     # field names changed since last publish

    def update(self, changes: dict):
        """Merge field-level changes. Track dirty fields."""
        for k, v in changes.items():
            if self.fields.get(k) != v:
                self.fields[k] = v
                self.dirty.add(k)
        self.sequence += 1
        self.timestamp = time.time()

    def get_delta(self) -> dict:
        """Return only changed fields, then clear dirty set."""
        delta = {k: self.fields[k] for k in self.dirty if k in self.fields}
        self.dirty.clear()
        return delta

    def snapshot(self) -> dict:
        """Full record — used on subscribe (LVC guarantee)."""
        return dict(self.fields)
