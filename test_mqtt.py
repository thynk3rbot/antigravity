import paho.mqtt.client as mqtt
import time


def on_message(client, userdata, message):
    print(f"Topic: {message.topic}, Message: {message.payload.decode()}")


client = mqtt.Client()
client.on_message = on_message
client.connect("localhost", 1883, 60)
client.subscribe("loralink/#")

print("Subscribed to loralink/#. Waiting for messages for 10 seconds...")
client.loop_start()
time.sleep(10)
client.loop_stop()
