import asyncio
import json
import socket
import logging
import sys
from datetime import datetime
from typing import Dict, Optional

try:
    import paho.mqtt.client as mqtt
    from rich.console import Console
    from rich.live import Live
    from rich.table import Table
    from rich.panel import Panel
    from rich.text import Text
    from rich.json import JSON
    from rich.layout import Layout
except ImportError:
    print("Error: Missing dependencies. Run 'pip install paho-mqtt rich'")
    sys.exit(1)

# Magic Branding
OCTOPUS_LOGO = """
      .---.
     /     \\
    | ( ) ( ) |  MAGIC
     \\  \"  /
      '---'
"""

console = Console()

class MagicClient:
    """
    Magic Client: Native MQTT Observer.
    Branded, high-performance CLI for real-time mesh telemetry.
    """
    def __init__(self, broker="localhost", port=1883):
        self.broker = broker
        self.port = port
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "MagicClient")
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        self.msg_history = []
        self.max_history = 15
        self.is_running = True

    def on_connect(self, client, userdata, flags, rc, props=None):
        if rc == 0:
            console.print(f"[bold green]✓ Connected to Magic Bus ({self.broker}:{self.port})[/]")
            client.subscribe("magic/#")
            client.subscribe("MagicCache/#")
        else:
            console.print(f"[bold red]✗ Connection failed (rc={rc})[/]")

    def on_message(self, client, userdata, msg):
        try:
            topic = msg.topic
            payload = msg.payload.decode("utf-8")
            try:
                data = json.loads(payload)
                formatted_data = JSON(payload)
            except:
                formatted_data = Text(payload, style="dim")
            
            timestamp = datetime.now().strftime("%H:%M:%S")
            self.msg_history.append({
                "time": timestamp,
                "topic": topic,
                "data": formatted_data
            })
            
            if len(self.msg_history) > self.max_history:
                self.msg_history.pop(0)
                
        except Exception as e:
            pass

    def generate_table(self) -> Table:
        table = Table(box=None, expand=True)
        table.add_column("Time", style="cyan", width=10)
        table.add_column("Topic", style="magenta", width=30)
        table.add_column("Magic Payload", style="white")

        for row in reversed(self.msg_history):
            table.add_row(row["time"], row["topic"], row["data"])
        
        return table

    def run(self):
        console.clear()
        console.print(Panel(Text(OCTOPUS_LOGO, style="bold cyan", justify="center"), title="[bold white]Magic Client v1.0[/]"))
        
        try:
            self.client.connect(self.broker, self.port, 60)
            self.client.loop_start()
            
            with Live(self.generate_table(), refresh_per_second=4) as live:
                while self.is_running:
                    live.update(self.generate_table())
                    import time
                    time.sleep(0.2)
                    
        except KeyboardInterrupt:
            self.is_running = False
        except Exception as e:
            console.print(f"[bold red]Error: {e}[/]")
        finally:
            self.client.loop_stop()
            self.client.disconnect()

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Magic Native Client")
    parser.add_argument("--host", default="localhost", help="MQTT Broker host")
    parser.add_argument("--port", type=int, default=1883, help="MQTT Broker port")
    args = parser.parse_args()

    client = MagicClient(broker=args.host, port=args.port)
    client.run()
