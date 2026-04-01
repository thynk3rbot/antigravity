from abc import ABC, abstractmethod
from .mx_message import MxMessage

class MxTransport(ABC):
    @abstractmethod
    def name(self) -> str: ...

    @abstractmethod
    async def send(self, msg: MxMessage) -> bool: ...

    @abstractmethod
    async def start(self): ...

    @abstractmethod
    async def stop(self): ...
