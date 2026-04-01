from enum import IntEnum
from dataclasses import dataclass, field
from typing import Any

class MxOp(IntEnum):
    UPDATE = 0
    INSERT = 1
    REMOVE = 2
    SUBSCRIBE = 3
    UNSUBSCRIBE = 4
    EXECUTE = 5
    WALK = 6

@dataclass
class MxMessage:
    op: MxOp
    subject: str                 # PC side uses full string subjects
    payload: dict = field(default_factory=dict)
    src_transport: str = ""
    context: Any = None
