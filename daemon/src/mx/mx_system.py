import asyncio
import logging
from .mx_bus import MxBus

log = logging.getLogger("mx.system")

class MxSystem:
    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(MxSystem, cls).__new__(cls)
            cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self.bus = MxBus()
        self._initialized = True

    async def start(self):
        log.info("Mx Framework starting...")
        # Future: start background tasks for bus processing if needed
        # For now, bus is reactive (publish deliveres directly to queues)

    async def stop(self):
        log.info("Mx Framework stopping...")
