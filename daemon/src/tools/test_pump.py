# daemon/src/tools/test_pump.py
import asyncio
import random
import logging
import time
import argparse
from typing import Dict, Any

# Ensure project root is in path
import os
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from mx.mx_bus import MxBus
from mx.mx_message import MxMessage, MxOp
from mx.mqtt_bridge import MqttBridge

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
log = logging.getLogger("test_pump")

class TestPump:
    def __init__(self, bus: MxBus, node_id: str = "PUMP-001"):
        self.bus = bus
        self.node_id = node_id
        self.running = False
        
    async def start(self, interval: float = 1.0, storm: bool = False):
        self.running = True
        log.info(f"Starting Test Pump (Interval: {interval}s, Storm: {storm})")
        
        count = 0
        while self.running:
            # 1. Heartbeat
            await self._pump("heartbeat", {"uptime": count, "rssi": -45})
            
            # 2. Node Status
            await self._pump("node_status", {
                "id": self.node_id,
                "vbat": 4.15 + random.uniform(-0.1, 0.1),
                "state": "RUNNING"
            })
            
            # 3. Sensor Data
            await self._pump("sensor_data", {
                "temp": 24.5 + random.uniform(-2, 2),
                "humidity": 45 + random.uniform(-10, 10),
                "lux": 150 + random.uniform(-50, 50)
            })
            
            # 4. GPS Position
            await self._pump("gps_position", {
                "lat": 18.334 + random.uniform(-0.01, 0.01),
                "lon": -64.931 + random.uniform(-0.01, 0.01),
                "alt": 150 + random.uniform(-10, 10)
            })
            
            count += 1
            if not storm:
                await asyncio.sleep(interval)
            else:
                # In storm mode, we don't sleep (or sleep very little)
                await asyncio.sleep(0.01)

    async def _pump(self, subject: str, payload: Dict[str, Any]):
        msg = MxMessage(
            op=MxOp.UPDATE,
            subject=subject,
            payload=payload,
            src_transport="pump"
        )
        await self.bus.publish(mx_msg=msg)
        log.debug(f"Pumped {subject}")

async def main():
    parser = argparse.ArgumentParser(description="V3 Test Pump")
    parser.add_argument("--interval", type=float, default=1.0, help="Message interval (seconds)")
    parser.add_argument("--storm", action="store_true", help="Run in storm mode (high speed)")
    parser.add_argument("--mqtt", action="store_true", help="Enable MQTT bridge")
    parser.add_argument("--broker", type=str, default="localhost", help="MQTT broker address")
    args = parser.parse_args()

    bus = MxBus()
    pump = TestPump(bus)
    
    tasks = []
    if args.mqtt:
        bridge = MqttBridge(bus, broker=args.broker)
        await bridge.start()
        
    tasks.append(asyncio.create_task(pump.start(interval=args.interval, storm=args.storm)))
    
    try:
        await asyncio.gather(*tasks)
    except KeyboardInterrupt:
        log.info("Stopping...")
        pump.running = False
        if args.mqtt:
            bridge.stop()

if __name__ == "__main__":
    asyncio.run(main())
