import pytest
from tools.daemon.models import Node, Message, Transport, MessageStatus


def test_node_creation():
    node = Node(id="node1", name="TestNode", type="wifi", address="192.168.1.50")
    assert node.id == "node1"
    assert node.name == "TestNode"
    assert node.type == "wifi"
    assert node.address == "192.168.1.50"
    assert node.online == True
    assert node.last_seen is not None


def test_message_creation():
    msg = Message(src="node1", dest="node2", command="GPIO 5 HIGH", status="QUEUED")
    assert msg.src == "node1"
    assert msg.dest == "node2"
    assert msg.command == "GPIO 5 HIGH"
    assert msg.status == MessageStatus.QUEUED


def test_transport_enum():
    assert Transport.SERIAL.value == "serial"
    assert Transport.HTTP.value == "http"
    assert Transport.BLE.value == "ble"
    assert Transport.LORA.value == "lora"
    assert Transport.MQTT.value == "mqtt"
