# daemon/src/mx/mx_bus.py
import asyncio
import logging
from collections import defaultdict
from .mx_message import MxMessage
from .mx_consumer import MxConsumer
from .mx_queue import MxQueue

log = logging.getLogger("mx.bus")

class MxBus:
    def __init__(self):
        self._subscriptions: dict[str, list[tuple[MxConsumer, MxQueue]]] = defaultdict(list)

    def subscribe(self, subject: str, consumer: MxConsumer, queue: MxQueue):
        self._subscriptions[subject].append((consumer, queue))
        log.info(f"subscribe: {subject} -> {type(consumer).__name__}")

    def unsubscribe(self, subject: str, consumer: MxConsumer):
        if subject in self._subscriptions:
            self._subscriptions[subject] = [
                (c, q) for c, q in self._subscriptions[subject] if c is not consumer
            ]

    async def publish(self, msg: MxMessage) -> int:
        """Deliver message to all subscriber queues for this subject."""
        subs = self._subscriptions.get(msg.subject, [])
        delivered = 0
        for consumer, queue in subs:
            if await queue.post(msg):
                delivered += 1
            else:
                log.warning(f"queue full: {queue.name} dropped {msg.op.name}")
        return delivered

    def subscriber_count(self, subject: str) -> int:
        return len(self._subscriptions.get(subject, []))
