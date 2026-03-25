import pytest
from unittest.mock import AsyncMock, MagicMock
from tools.webapp.daemon_client import DaemonClient


@pytest.mark.asyncio
async def test_daemon_client_health_ok():
    """health() returns True when daemon responds 200"""
    client = DaemonClient()
    client.session = MagicMock()

    mock_response = AsyncMock()
    mock_response.status = 200
    mock_response.__aenter__ = AsyncMock(return_value=mock_response)
    mock_response.__aexit__ = AsyncMock(return_value=False)
    client.session.get = MagicMock(return_value=mock_response)

    result = await client.health()
    assert result is True


@pytest.mark.asyncio
async def test_daemon_client_list_nodes_returns_list():
    """list_nodes() returns empty list on error (never throws)"""
    client = DaemonClient()
    client.session = MagicMock()

    # Simulate connection error
    client.session.get = MagicMock(side_effect=Exception("Connection refused"))

    result = await client.list_nodes()
    assert result == []


@pytest.mark.asyncio
async def test_daemon_client_send_command_returns_false_on_failure():
    """send_command() returns False on HTTP error"""
    client = DaemonClient()
    client.session = MagicMock()

    mock_response = AsyncMock()
    mock_response.status = 500
    mock_response.__aenter__ = AsyncMock(return_value=mock_response)
    mock_response.__aexit__ = AsyncMock(return_value=False)
    client.session.post = MagicMock(return_value=mock_response)

    result = await client.send_command("node1", "GPIO 5 HIGH")
    assert result is False
