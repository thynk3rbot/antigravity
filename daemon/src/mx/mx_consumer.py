from abc import ABC, abstractmethod
from .mx_message import MxMessage

class MxConsumer(ABC):
    @abstractmethod
    async def consume(self, msg: MxMessage) -> bool:
        """Process a message. Returns True if consumed successfully."""
        ...
