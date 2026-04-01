import asyncio
from .mx_message import MxMessage

class MxQueue:
    def __init__(self, name: str, maxsize: int = 64):
        self.name = name
        self._queue: asyncio.Queue[MxMessage] = asyncio.Queue(maxsize=maxsize)

    async def post(self, msg: MxMessage) -> bool:
        try:
            self._queue.put_nowait(msg)
            return True
        except asyncio.QueueFull:
            return False

    async def receive(self, timeout: float = None) -> MxMessage | None:
        try:
            if timeout is None:
                return await self._queue.get()
            return await asyncio.wait_for(self._queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None

    def pending(self) -> int:
        return self._queue.qsize()
