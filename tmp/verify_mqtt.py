import paho.mqtt.client as mqtt
import json
import time

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe("magic/+/telemetry")
    client.subscribe("magic/+/status")
    client.subscribe("magic/+/mx/#")

def on_message(client, userdata, msg):
    print(f"\n[MQTT] Topic: {msg.topic}")
    try:
        data = json.loads(msg.payload.decode())
        print(json.dumps(data, indent=2))
    except:
        print(f"Raw: {msg.payload}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

print("Connecting to localhost:1883...")
client.connect("localhost", 1883, 60)

print("Watching for 60 seconds...")
client.loop_start()
time.sleep(60)
client.loop_stop()
