import pytest
from unittest.mock import AsyncMock, patch, MagicMock
from tools.daemon.transport import TransportManager
from tools.daemon.models import Transport, Node


@pytest.mark.asyncio
async def test_transport_manager_select_http():
    """Test HTTP transport selection for WiFi node"""
    manager = TransportManager()

    node = Node(id="node1", name="WiFi Node", type="wifi", address="192.168.1.50")

    # Mock HTTP availability
    with patch.object(manager, '_probe_http', new_callable=AsyncMock) as mock_http:
        mock_http.return_value = True

        selected = await manager.select_transport(node)
        assert selected == Transport.HTTP


@pytest.mark.asyncio
async def test_transport_manager_fallback_to_ble():
    """Test fallback from HTTP to BLE when HTTP fails"""
    manager = TransportManager()

    node = Node(id="node1", name="WiFi Node", type="wifi", address="192.168.1.50")

    # Mock HTTP fail, BLE success
    with patch.object(manager, '_probe_http', new_callable=AsyncMock) as mock_http:
        with patch.object(manager, '_probe_ble', new_callable=AsyncMock) as mock_ble:
            mock_http.return_value = False
            mock_ble.return_value = True

            selected = await manager.select_transport(node)
            assert selected == Transport.BLE
