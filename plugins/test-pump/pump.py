"""
Magic Test Data Pump — Simulates device fleet telemetry.
Publishes to MQTT using the same topic contract as real firmware.

Usage:
    python pump.py [--scenario healthy_fleet] [--interval 5] [--broker localhost]
"""

import argparse
import json
import logging
import os
import random
import time
import signal
import sys
from pathlib import Path
from typing import Dict, Any

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("test-pump")

MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
SCENARIO_DIR = Path(__file__).parent / "scenarios"
DEFAULT_INTERVAL = float(os.getenv("PUMP_INTERVAL", "5"))

class MagicPump:
    def __init__(self, broker: str, port: int, interval: float):
        self.broker = broker
        self.port = port
        self.interval = interval
        self.client = mqtt.Client(CallbackAPIVersion.VERSION2)
        self.running = True
        self.start_time = time.time()
        self.devices = []

        # Setup MQTT callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, rc, properties):
        if rc == 0:
            log.info(f"Connected to MQTT broker at {self.broker}:{self.port}")
        else:
            log.error(f"Failed to connect to MQTT, rc={rc}")

    def _on_disconnect(self, client, userdata, flags, rc, properties):
        log.warning("Disconnected from MQTT broker")

    def load_scenario(self, scenario_name: str):
        """Load a scenario JSON file."""
        scenario_path = SCENARIO_DIR / f"{scenario_name}.json"
        if not scenario_path.exists():
            log.error(f"Scenario not found: {scenario_path}")
            sys.exit(1)

        with open(scenario_path, "r") as f:
            self.scenario_data = json.load(f)
            self.devices = self.scenario_data.get("devices", [])
            log.info(f"Loaded scenario: {self.scenario_data['name']} ({len(self.devices)} devices)")

    def generate_telemetry(self, device_state: dict, elapsed_s: float) -> dict:
        """Generate one telemetry payload for a simulated device."""
        # Battery drains linearly
        drain = device_state["battery_drain_mv_per_hour"] * (elapsed_s / 3600)
        battery_mv = max(3000, device_state["battery_mv_start"] - drain)

        # Battery percentage: linear map 4200mv=100%, 3000mv=0%
        battery_pct = max(0, min(100, int((battery_mv - 3000) / 12)))

        # RSSI jitters around center
        rssi = device_state["rssi_center"] + random.randint(
            -device_state["rssi_jitter"],
            device_state["rssi_jitter"]
        )

        # Uptime increments
        uptime_ms = int(elapsed_s * 1000)

        payload = {
            "uptime_ms": uptime_ms,
            "battery_mv": int(battery_mv),
            "battery_pct": battery_pct,
            "rssi": rssi,
            "neighbors": device_state.get("neighbors", []),
            "relay_1": device_state.get("relay_1", False),
            "relay_2": device_state.get("relay_2", False),
            "free_heap": random.randint(180000, 220000),
            "version": "0.0.22V4",
        }

        if device_state.get("gps"):
            # Slight GPS drift for realism
            gps = device_state["gps"]
            payload["gps"] = {
                "lat": gps["lat"] + random.uniform(-0.0001, 0.0001),
                "lon": gps["lon"] + random.uniform(-0.0001, 0.0001),
                "alt": gps["alt"] + random.uniform(-0.5, 0.5),
            }

        return payload

    def run(self):
        """Main publish loop."""
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()

            log.info(f"Pump starting... (publishing every {self.interval}s)")
            while self.running:
                elapsed_s = time.time() - self.start_time
                
                for device in self.devices:
                    node_id = device["node_id"]
                    telemetry = self.generate_telemetry(device, elapsed_s)
                    
                    # Firmware Contract topics
                    topic_telemetry = f"magic/{node_id}/telemetry"
                    topic_status = f"magic/{node_id}/status"

                    self.client.publish(topic_telemetry, json.dumps(telemetry))
                    self.client.publish(topic_status, "ONLINE")

                    log.info(f"Published telemetry for {node_id}")

                time.sleep(self.interval)
        except KeyboardInterrupt:
            self.stop()
        except Exception as e:
            log.error(f"Run error: {e}")
            self.stop()

    def stop(self, *args):
        log.info("Stopping pump gracefully...")
        self.running = False
        self.client.loop_stop()
        self.client.disconnect()
        sys.exit(0)

def main():
    parser = argparse.ArgumentParser(description="Magic Test Data Pump")
    parser.add_argument("--scenario", type=str, default=os.getenv("PUMP_SCENARIO", "healthy_fleet"), help="Scenario name")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL, help="Publish interval in seconds")
    parser.add_argument("--broker", type=str, default=MQTT_BROKER, help="MQTT broker address")
    parser.add_argument("--port", type=int, default=MQTT_PORT, help="MQTT broker port")
    args = parser.parse_args()

    pump = MagicPump(args.broker, args.port, args.interval)
    
    # Handle signals
    signal.signal(signal.SIGINT, pump.stop)
    signal.signal(signal.SIGTERM, pump.stop)

    pump.load_scenario(args.scenario)
    pump.run()

if __name__ == "__main__":
    main()
