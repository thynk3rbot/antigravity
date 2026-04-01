import asyncio
import sys
import os

# Add src to path if needed (assuming run from daemon root or Code root)
sys.path.append(os.path.join(os.path.dirname(__file__)))

from mx.mx_bus import MxBus
from mx.mx_queue import MxQueue
from mx.mx_message import MxMessage, MxOp
from mx.mx_consumer import MxConsumer
from mx.mx_record import MxRecord

class TestConsumer(MxConsumer):
    def __init__(self):
        self.received = []

    async def consume(self, msg: MxMessage) -> bool:
        self.received.append(msg)
        return True

async def main():
    print("Starting Mx Daemon smoke tests...")
    bus = MxBus()
    consumer = TestConsumer()
    queue = MxQueue("test", maxsize=8)
    
    # Subscribe
    bus.subscribe("node_status", consumer, queue)
    assert bus.subscriber_count("node_status") == 1

    # Publish
    msg = MxMessage(op=MxOp.UPDATE, subject="node_status", payload={"battery_mv": 3700})
    delivered = await bus.publish(msg)
    assert delivered == 1

    # Receive from queue
    received = await queue.receive(timeout=1.0)
    assert received is not None
    assert received.op == MxOp.UPDATE
    assert received.payload["battery_mv"] == 3700

    # LVC (Last Value Cache) test
    rec = MxRecord(subject="node_status")
    rec.update({"battery_mv": 3700, "rssi": -45})
    assert rec.get_delta() == {"battery_mv": 3700, "rssi": -45}
    assert rec.get_delta() == {}    # dirty cleared after get_delta

    rec.update({"rssi": -50})
    assert rec.get_delta() == {"rssi": -50}

    # Snapshot test
    assert rec.snapshot() == {"battery_mv": 3700, "rssi": -50}

    print("All Mx smoke tests passed!")

if __name__ == "__main__":
    asyncio.run(main())

