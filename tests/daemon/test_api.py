import pytest
import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
from tools.daemon.api import create_api_app
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager


@pytest.fixture
def test_app(tmp_path):
    """Fixture: create test app with temp SQLite DB that persists for the test"""
    queue = MessageQueue(tmp_path / "test.db")
    transport = TransportManager()
    app = create_api_app(queue, transport)
    return app, queue


def test_health_endpoint(test_app):
    """GET /health returns ok status"""
    app, _ = test_app
    client = TestClient(app)
    response = client.get("/health")
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "ok"
    assert "nodes_count" in data


def test_add_and_list_nodes(test_app):
    """POST /api/nodes registers node, GET /api/nodes returns it"""
    app, _ = test_app
    client = TestClient(app)

    # Register a node
    response = client.post("/api/nodes", json={
        "id": "node1",
        "name": "Test Node",
        "type": "wifi",
        "address": "192.168.1.50"
    })
    assert response.status_code == 200
    data = response.json()
    assert data["id"] == "node1"
    assert data["name"] == "Test Node"

    # List nodes — should contain it
    response = client.get("/api/nodes")
    assert response.status_code == 200
    nodes = response.json()
    assert len(nodes) == 1
    assert nodes[0]["id"] == "node1"


def test_get_node_not_found(test_app):
    """GET /api/nodes/{id} returns 404 for unknown node"""
    app, _ = test_app
    client = TestClient(app)
    response = client.get("/api/nodes/nonexistent")
    assert response.status_code == 404
