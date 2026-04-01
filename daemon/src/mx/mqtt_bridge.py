# daemon/src/mx/mqtt_bridge.py
import json
import logging
import asyncio
import paho.mqtt.client as mqtt
from .mx_message import MxMessage, MxOp
from .mx_consumer import MxConsumer
from .mx_bus import MxBus
from .mx_queue import MxQueue

log = logging.getLogger("mx.mqtt")

class MqttBridge(MxConsumer):
    def __init__(self, bus: MxBus, broker: str = "localhost", port: int = 1883):
        self.bus = bus
        self.broker = broker
        self.port = port
        self.client = mqtt.Client()
        self._connected = False
        self._queue = MxQueue("mqtt_out", max_size=100)

    async def start(self):
        """Connect to MQTT and start the background loop."""
        try:
            self.client.on_connect = self._on_connect
            self.client.on_message = self._on_mqtt_message
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            
            # Subscribe to all subjects on the internal bus
            from .mx_subjects import SUBJECTS
            for sub in SUBJECTS.values():
                self.bus.subscribe(sub, self, self._queue)
            
            log.info(f"Connected to MQTT broker at {self.broker}:{self.port}")
            self._connected = True
            
            # Run the consumer loop
            asyncio.create_task(self._process_outbound())
        except Exception as e:
            log.error(f"MQTT connection failed: {e}")

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe("mx/v3/+/down")
            log.info("Subscribed to MQTT command topics")
        else:
            log.error(f"MQTT connect failed with rc={rc}")

    def _on_mqtt_message(self, client, userdata, msg):
        """Handle incoming MQTT messages (Commands -> MxBus)."""
        try:
            topic_parts = msg.topic.split('/')
            subject = topic_parts[2]
            payload = json.loads(msg.payload.decode())
            
            mx_msg = MxMessage(
                op=MxOp.EXECUTE,
                subject=subject,
                payload=payload,
                src_transport="mqtt"
            )
            asyncio.create_task(self.bus.publish(mx_msg))
            log.debug(f"MQTT -> Mx: {subject}")
        except Exception as e:
            log.error(f"Error handling MQTT message: {e}")

    async def consume(self, msg: MxMessage) -> bool:
        """Called by MxBus when a message is published internally."""
        # The message is already in our queue due to the subscription.
        # We just return True here.
        return True

    async def _process_outbound(self):
        """Forward Mx messages from the queue to MQTT."""
        while True:
            msg = await self._queue.get()
            if not msg:
                break
            
            if msg.src_transport == "mqtt":
                continue # Prevent loops
                
            topic = f"mx/v3/{msg.subject}/up"
            payload = {
                "op": msg.op.name,
                "subject": msg.subject,
                "payload": msg.payload
            }
            
            self.client.publish(topic, json.dumps(payload))
            log.debug(f"Mx -> MQTT: {topic}")

    def stop(self):
        self.client.loop_stop()
        self.client.disconnect()
