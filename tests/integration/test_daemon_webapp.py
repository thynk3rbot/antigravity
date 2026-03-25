"""
Integration test: Full command flow through daemon API.

Uses FastAPI TestClient to simulate webapp calling daemon endpoints,
bypassing real HTTP so the test runs without a live daemon process.
Tests the full chain: node registration -> command routing -> message persistence.
"""
import pytest
import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
from tools.daemon.api import create_api_app
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager


@pytest.fixture
def daemon_app(tmp_path):
    """Create a fully wired daemon app with temp DB."""
    queue = MessageQueue(tmp_path / "integration.db")
    transport = TransportManager()
    app = create_api_app(queue, transport)
    return app, queue


def test_full_command_flow(daemon_app):
    """Full flow: register node -> send command -> verify message persisted."""
    app, queue = daemon_app
    client = TestClient(app)

    # Step 1: Register a node
    resp = client.post("/api/nodes", json={
        "id": "esp32-alpha",
        "name": "Alpha Node",
        "type": "wifi",
        "address": "192.168.1.100"
    })
    assert resp.status_code == 200
    assert resp.json()["id"] == "esp32-alpha"

    # Step 2: Send a command
    resp = client.post("/api/command", json={
        "dest": "esp32-alpha",
        "command": "RELAY 1 ON"
    })
    assert resp.status_code == 200
    result = resp.json()
    assert "id" in result
    assert result["status"] in ("SENT", "FAILED")  # Transport is stubbed — either is valid
    msg_id = result["id"]

    # Step 3: Verify message was persisted to SQLite
    msg = queue.get_message(msg_id)
    assert msg is not None
    assert msg.dest == "esp32-alpha"
    assert msg.command == "RELAY 1 ON"


def test_command_to_unknown_node_returns_404(daemon_app):
    """Sending command to unregistered node should return 404."""
    app, _ = daemon_app
    client = TestClient(app)

    resp = client.post("/api/command", json={
        "dest": "ghost-node",
        "command": "LED ON"
    })
    assert resp.status_code == 404


def test_message_history_queryable_after_command(daemon_app):
    """After sending a command, it should appear in message history."""
    app, _ = daemon_app
    client = TestClient(app)

    # Register and send
    client.post("/api/nodes", json={
        "id": "node-bravo",
        "name": "Bravo",
        "type": "wifi",
        "address": "192.168.1.101"
    })
    client.post("/api/command", json={
        "dest": "node-bravo",
        "command": "GPIO 5 HIGH"
    })

    # Query message history
    resp = client.get("/api/messages", params={"dest": "node-bravo"})
    assert resp.status_code == 200
    messages = resp.json()
    assert len(messages) >= 1
    assert messages[0]["dest"] == "node-bravo"
    assert messages[0]["command"] == "GPIO 5 HIGH"
