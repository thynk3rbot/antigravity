from enum import Enum
from dataclasses import dataclass, field
from typing import Optional
import time
import uuid


class Transport(Enum):
    """Available transport protocols"""
    SERIAL = "serial"
    HTTP = "http"
    BLE = "ble"
    LORA = "lora"
    MQTT = "mqtt"


class MessageStatus(Enum):
    """Message delivery status"""
    QUEUED = "queued"
    SENT = "sent"
    ACKED = "acked"
    FAILED = "failed"
    DELIVERED = "delivered"


@dataclass
class Node:
    """Device node in the swarm"""
    id: str
    name: str
    type: str  # wifi, serial, ble, lora
    address: str
    hardware: str = "Unknown"
    mac: str = "Unknown"
    online: bool = True
    last_seen: float = field(default_factory=time.time)
    preferred_transport: Optional[Transport] = None

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "address": self.address,
            "hardware": self.hardware,
            "mac": self.mac,
            "online": self.online,
            "last_seen": self.last_seen,
            "preferred_transport": self.preferred_transport.value if self.preferred_transport else None,
        }


@dataclass
class Message:
    """Command message in the queue"""
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    src: str = "daemon"
    dest: str = ""
    command: str = ""
    status: MessageStatus = MessageStatus.QUEUED
    created_at: float = field(default_factory=time.time)
    sent_at: Optional[float] = None
    acked_at: Optional[float] = None
    transport_used: Optional[Transport] = None
    retry_count: int = 0

    def __post_init__(self):
        """Convert status string to enum if needed"""
        if isinstance(self.status, str):
            self.status = MessageStatus(self.status.lower())

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "src": self.src,
            "dest": self.dest,
            "command": self.command,
            "status": self.status.value,
            "created_at": self.created_at,
            "sent_at": self.sent_at,
            "acked_at": self.acked_at,
            "transport_used": self.transport_used.value if self.transport_used else None,
            "retry_count": self.retry_count,
        }
