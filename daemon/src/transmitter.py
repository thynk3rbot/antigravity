import json
import time
import random
import paho.mqtt.client as mqtt
from monitor import monitor

class MagicTransmitter:
    def __init__(self, host="localhost", port=1883):
        self.client = mqtt.Client("MagicTransmitter")
        self.host = host
        self.port = port
        self.topic_prefix = "MagicCache"

    def connect(self):
        try:
            self.client.connect(self.host, self.port, 60)
            monitor.log_info("Transmitter", f"Connected to {self.host}:{self.port}")
        except Exception as e:
            monitor.log_error("Transmitter", f"Connection failed: {e}")

    def send_market_data(self, ticker, price_range=(100, 200)):
        """Spoof financial market updates."""
        topic = f"{self.topic_prefix}/IMDF/QUOTE/{ticker}"
        payload = {
            "price": round(random.uniform(*price_range), 2),
            "bid": round(random.uniform(*price_range) - 0.5, 2),
            "ask": round(random.uniform(*price_range) + 0.5, 2),
            "volume": random.randint(1000, 50000),
            "timestamp": time.time()
        }
        self._publish(topic, payload)

    def send_iot_telemetry(self, node_id):
        """Spoof sensor data."""
        topic = f"{self.topic_prefix}/IOT/FLEET/{node_id}"
        payload = {
            "battery_mv": random.randint(3300, 4200),
            "rssi": random.randint(-100, -40),
            "status": "online",
            "uptime_ms": 1000000 + random.randint(0, 100000)
        }
        self._publish(topic, payload)

    def _publish(self, topic, payload):
        self.client.publish(topic, json.dumps(payload))
        monitor.log_info("Transmitter", f"PUBLISHED: {topic}")

if __name__ == "__main__":
    transmitter = MagicTransmitter()
    transmitter.connect()
    
    # Send continuous spoofed data
    tickers = ["MSFT", "AAPL", "GOOGL", "AMZN"]
    while True:
        ticker = random.choice(tickers)
        transmitter.send_market_data(ticker)
        transmitter.send_iot_telemetry(f"node-{random.randint(20, 40)}")
        time.sleep(2)
