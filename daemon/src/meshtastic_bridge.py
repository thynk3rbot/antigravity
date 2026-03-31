import time
import argparse
import json
import logging
from pubsub import pub
import meshtastic
import meshtastic.serial_interface
import meshtastic.tcp_interface
import serial.tools.list_ports
import paho.mqtt.client as mqtt

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

# Device discovery criteria
TBEAM_CHIPSETS = ["CP210", "CH34", "CH91", "USB-SERIAL"]
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_PORT = 1883

# MQTT Setup (paho-mqtt 2.1 compatible)
try:
    from paho.mqtt.enums import CallbackAPIVersion
    mqtt_client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id="meshtastic_bridge")
except ImportError:
    # Fallback for older paho-mqtt < 2.0
    mqtt_client = mqtt.Client(client_id="meshtastic_bridge")

mqtt_connected = False

def on_mqtt_connect(client, userdata, flags, rc):
    global mqtt_connected
    if rc == 0:
        logging.info("Connected to local Mosquitto broker.")
        mqtt_connected = True
    else:
        logging.error(f"Failed to connect to Mosquitto: {rc}")

def on_mqtt_disconnect(client, userdata, rc):
    global mqtt_connected
    mqtt_connected = False
    logging.warning("Disconnected from Mosquitto.")

# Dictionary to hold the state of foreign meshtastic nodes
node_states = {}

def on_receive(packet, interface):
    try:
        node_id = packet.get('fromId', 'Unknown')
        
        if node_id not in node_states:
            node_states[node_id] = {
                "uptime_ms": 0,
                "battery_mv": 3700,
                "gps": None
            }

        if 'decoded' in packet and packet['decoded']['portnum'] == 'TELEMETRY_APP':
            metrics = packet['decoded'].get('telemetry', {}).get('deviceMetrics', {})
            if metrics:
                node_states[node_id]["uptime_ms"] = metrics.get('uptimeSeconds', 0) * 1000
                node_states[node_id]["battery_mv"] = int(metrics.get('voltage', 3.7) * 1000)
                
                # Publish native Magic v2 telemetry envelope
                data = {
                    "node_id": node_id,
                    "hardware": "TBEAM_EXT",
                    "uptime_ms": node_states[node_id]["uptime_ms"],
                    "battery_mv": node_states[node_id]["battery_mv"],
                    "neighbors": []
                }
                
                if node_states[node_id]["gps"]:
                    data["gps"] = node_states[node_id]["gps"]
                    
                topic = f"magic/{node_id}/telemetry"
                logging.info(f"Spoofing native telemetry to {topic}")
                if mqtt_connected:
                    mqtt_client.publish(topic, json.dumps(data), qos=1)

        elif 'decoded' in packet and packet['decoded']['portnum'] == 'POSITION_APP':
            pos = packet['decoded']['position']
            
            gps_data = {
                "lat": pos.get("latitude", 0.0),
                "lon": pos.get("longitude", 0.0),
                "alt": pos.get("altitude", 0.0),
                "sats": pos.get("satsInView", 0)
            }
            node_states[node_id]["gps"] = gps_data
            
            # Combine into a native node payload
            data = {
                "node_id": node_id,
                "hardware": "TBEAM_EXT",
                "uptime_ms": node_states[node_id].get("uptime_ms", 10000),
                "battery_mv": node_states[node_id].get("battery_mv", 3700),
                "neighbors": [],
                "gps": gps_data
            }
            
            topic = f"magic/{node_id}/telemetry"
            logging.info(f"Spoofing native GPS data to {topic}: {gps_data['lat']}, {gps_data['lon']}")
            
            if mqtt_connected:
                mqtt_client.publish(topic, json.dumps(data), qos=1)
                
        elif 'decoded' in packet and packet['decoded']['portnum'] == 'WAYPOINT_APP':
            wp = packet['decoded'].get('waypoint', {})
            
            lat = wp.get("latitudeI", 0) / 1e7 if "latitudeI" in wp else wp.get("latitude", 0.0)
            lon = wp.get("longitudeI", 0) / 1e7 if "longitudeI" in wp else wp.get("longitude", 0.0)
            
            data = {
                "node_id": node_id,
                "name": wp.get("name", "Unknown Waypoint"),
                "description": wp.get("description", ""),
                "lat": lat,
                "lon": lon,
                "timestamp": packet.get("rxTime", int(time.time()))
            }
            
            logging.info(f"Intercepted Waypoint '{data['name']}' at {lat},{lon}")
            
            if mqtt_connected:
                mqtt_client.publish("magic/meshtastic/waypoints", json.dumps(data), qos=1)
                
    except Exception as e:
        logging.error(f"Error parsing packet: {e}")

def find_tbeam_port():
    """Auto-detect LilyGo T-Beam by common USB-to-UART bridge descriptors."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = port.description.upper()
        if any(chip in desc for chip in TBEAM_CHIPSETS):
            logging.info(f"Auto-detected LilyGo T-Beam on {port.device} ({port.description})")
            return port.device
    return None

def main():
    parser = argparse.ArgumentParser(description="Antigravity Meshtastic Sentinel Bridge")
    parser.add_argument("--host", type=str, help="IP or hostname of the network-connected T-Beam")
    parser.add_argument("--ssid", type=str, help="WiFi SSID to provision (requires USB connection)")
    parser.add_argument("--password", type=str, help="WiFi Password to provision (requires USB connection)")
    args = parser.parse_args()

    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    logging.info(f"Connecting to MQTT broker at {MQTT_BROKER}...")
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        logging.error(f"MQTT init failed: {e}. Running without MQTT.")

    pub.subscribe(on_receive, "meshtastic.receive")

    interface = None
    try:
        if args.host:
            # Connect over Network
            logging.info(f"Connecting to Meshtastic Node on {args.host} (TCP)...")
            interface = meshtastic.tcp_interface.TCPInterface(args.host)
        else:
            # Connect over Serial
            serial_port = find_tbeam_port()
            if not serial_port:
                logging.error("Could not auto-detect LilyGo T-Beam. Check your USB connection!")
                return
            logging.info(f"Connecting to Meshtastic Node on {serial_port} (Serial)...")
            interface = meshtastic.serial_interface.SerialInterface(serial_port)

        logging.info("Connected to Meshtastic Node successfully.")

        # WiFi Provisioning (If requested)
        if args.ssid and args.password:
            try:
                logging.info(f"Provisioning WiFi: SSID='{args.ssid}'...")
                # Modern Meshtastic API for network settings
                interface.localNode.network.wifi_ssid = args.ssid
                interface.localNode.network.wifi_psk = args.password
                interface.localNode.network.wifi_enabled = True
                interface.localNode.writeConfig("network")
                logging.info("WiFi credentials written! Restarting node to apply...")
                time.sleep(2)
            except Exception as e:
                logging.error(f"Failed to provision WiFi: {e}")
        
        # Ensure T-Beam acts optimally as a Magic Sentinel Bridge over USB
        try:
            node = interface.localNode
            changed = False
            
            # Disable Bluetooth to save processor/power (we use serial exclusively)
            if hasattr(node.localConfig, 'bluetooth') and node.localConfig.bluetooth.enabled:
                logging.info("Disabling Bluetooth for dedicated USB Sentinel mode...")
                node.localConfig.bluetooth.enabled = False
                node.writeConfig("bluetooth")
                changed = True

            # Disable Power Saving so it never sleeps
            if hasattr(node.localConfig, 'power') and getattr(node.localConfig.power, 'is_power_saving', False):
                logging.info("Disabling Power Saving mode...")
                node.localConfig.power.is_power_saving = False
                node.writeConfig("power")
                changed = True
                
            # Maximize Telemetry Broadcasts to feed the Magic Daemon
            if hasattr(node.moduleConfig, 'telemetry'):
                # 60 seconds is typical frequent polling
                if getattr(node.moduleConfig.telemetry, 'device_update_interval', 3600) > 60:
                    logging.info("Increasing Telemetry polling rate to 60s...")
                    node.moduleConfig.telemetry.device_update_interval = 60
                    node.writeConfig("telemetry")
                    changed = True
                
            if changed:
                logging.info("T-Beam Sentinel hardware optimizations applied!")
                time.sleep(3)
                
        except Exception as e:
            logging.error(f"Failed to auto-configure T-Beam Sentinel settings: {e}")
        
        # Keep alive loop
        while True:
            time.sleep(1)
            
    except Exception as e:
        logging.error(f"Failed to connect to Meshtastic: {e}")
    finally:
        if interface:
            interface.close()
        mqtt_client.loop_stop()

if __name__ == "__main__":
    main()
