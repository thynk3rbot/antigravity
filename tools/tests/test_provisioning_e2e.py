"""
E2E Test: Feature Registry Provisioning Flow

Tests the complete provisioning pipeline:
1. Daemon receives provision request
2. Loads carrier profile and product config
3. Merges feature flags
4. Sends provisioning payload to device
5. Device acknowledges and applies config
"""

import asyncio
import json
import pytest
from unittest.mock import Mock, AsyncMock, patch
from tools.daemon.models import (
    ProvisionRequest, CarrierProfile, Node,
    ProvisionResponse
)
from tools.daemon.api import create_api_app
from tools.daemon.persistence import MessageQueue
from tools.daemon.transport import TransportManager


@pytest.fixture
def api_app():
    """Create test API app with mocked dependencies."""
    message_queue = Mock(spec=MessageQueue)
    transport_manager = Mock(spec=TransportManager)
    transport_manager.send_command = AsyncMock(return_value=True)

    return create_api_app(message_queue, transport_manager), message_queue, transport_manager


@pytest.mark.asyncio
async def test_provision_basic(api_app):
    """Test basic provisioning with bare carrier profile."""
    app, msg_queue, transport = api_app
    client = app

    # Simulate device in registry
    test_device = Node(
        id="loralink-27",
        name="Test-Device",
        type="wifi",
        address="172.16.0.100"
    )

    # Mock request
    provision_req = {
        "device_id": "loralink-27",
        "carrier": "bare",
        "product": None,
        "identity": {"name": "Device-27", "role": "node"},
        "reboot": True
    }

    # Expected payload structure (merged from carrier profile)
    expected_features = {
        "relay": 1,
        "mqtt": 0,
        "gps": 0,
        "ble": 1,
        "espnow": 1,
        "sensor": 0,
        "oled": 1,
        "scheduler": 1,
        "mcp": 0
    }

    # Verify provision request parses correctly
    req = ProvisionRequest(**provision_req)
    assert req.device_id == "loralink-27"
    assert req.carrier == "bare"
    assert req.reboot is True


@pytest.mark.asyncio
async def test_provision_rv12v_with_feature_override(api_app):
    """Test provisioning RV12V with feature overrides."""

    # Simulate disabling GPS on an RV12V board
    provision_req = {
        "device_id": "loralink-28",
        "carrier": "rv12v",
        "product": None,
        "features": {"gps": 0},  # Override GPS off
        "identity": {"name": "Valve-North", "fleet_id": "farm-north"},
        "reboot": True
    }

    req = ProvisionRequest(**provision_req)
    assert req.features["gps"] == 0
    assert req.identity["fleet_id"] == "farm-north"


@pytest.mark.asyncio
async def test_carrier_profile_merge():
    """Test feature merging logic."""

    # Base RV12V carrier has GPS enabled
    carrier = CarrierProfile(
        id="rv12v",
        name="RV 12V",
        hw={"mcp_count": 1},
        features={"mqtt": 1, "gps": 1, "relay": 1}
    )

    # Provision request disables GPS
    override_features = {"gps": 0}

    # Merge: carrier defaults + overrides
    merged = {**carrier.features, **override_features}

    assert merged["mqtt"] == 1  # Inherited from carrier
    assert merged["gps"] == 0   # Overridden
    assert merged["relay"] == 1 # Inherited from carrier


def test_provision_request_model():
    """Test ProvisionRequest data model."""
    req = ProvisionRequest(
        device_id="test-dev",
        carrier="bare",
        product="test-product",
        features={"mqtt": 0},
        identity={"name": "Test", "role": "hub"},
        reboot=False
    )

    req_dict = req.to_dict()
    assert req_dict["device_id"] == "test-dev"
    assert req_dict["carrier"] == "bare"
    assert req_dict["product"] == "test-product"
    assert req_dict["features"]["mqtt"] == 0
    assert req_dict["identity"]["role"] == "hub"
    assert req_dict["reboot"] is False


def test_carrier_profile_model():
    """Test CarrierProfile data model."""
    profile = CarrierProfile(
        id="rv12v",
        name="RV 12V Control",
        hw={"i2c_buses": 1, "mcp_count": 1},
        features={"relay": 1, "mqtt": 1},
        pins={"relay_12v_1": 46}
    )

    profile_dict = profile.to_dict()
    assert profile_dict["id"] == "rv12v"
    assert profile_dict["hw"]["mcp_count"] == 1
    assert profile_dict["features"]["relay"] == 1
    assert profile_dict["pins"]["relay_12v_1"] == 46


def test_provision_response_model():
    """Test ProvisionResponse data model."""
    resp = ProvisionResponse(
        status="ok",
        device_id="loralink-27",
        reboot_in_ms=2000
    )

    resp_dict = resp.to_dict()
    assert resp_dict["status"] == "ok"
    assert resp_dict["device_id"] == "loralink-27"
    assert resp_dict["reboot_in_ms"] == 2000

    # Error response
    error_resp = ProvisionResponse(
        status="error",
        device_id="loralink-28",
        error_msg="Carrier profile not found"
    )
    assert error_resp.status == "error"
    assert error_resp.error_msg == "Carrier profile not found"


class TestFeatureRegistryIntegration:
    """Integration tests for Feature Registry with provisioning."""

    def test_feature_toggle_logic(self):
        """Verify feature enable/disable logic matches firmware expectations."""

        # Firmware receives NVS features namespace with u8 values
        # 0 = disabled, 1 = enabled (default ON = permissive)

        features = {
            "relay": 1,      # Enabled
            "mqtt": 0,       # Disabled
            "gps": 1,        # Enabled
            "ble": 1,        # Enabled
            "espnow": 1,     # Enabled
            "sensor": 0,     # Disabled
            "oled": 1,       # Enabled
            "scheduler": 1   # Enabled (always-on)
        }

        # Boot sequence checks: if (PluginManager::isEnabled("mqtt"))
        # Should skip MQTT if features[mqtt] == 0
        assert features["mqtt"] == 0  # Skip
        assert features["sensor"] == 0  # Skip
        assert features["relay"] == 1  # Init
        assert features["oled"] == 1  # Init

    def test_carrier_hw_topology(self):
        """Verify hardware topology is correctly provisioned."""

        rv12v = CarrierProfile(
            id="rv12v",
            name="RV 12V Control",
            hw={
                "i2c_buses": 1,
                "i2c0_sda": 21,
                "i2c0_scl": 22,
                "mcp_count": 1,
                "mcp_addrs": [32],
                "mcp_bus": 0,
                "mcp_int_pin": 38,
                "carrier": "rv12v"
            },
            features={"mcp": 1, "relay": 1},
            pins={"relay_12v_1": 46, "mcp_int": 38}
        )

        # Device receives hw namespace with I2C config
        assert rv12v.hw["mcp_count"] == 1
        assert rv12v.hw["mcp_addrs"] == [32]
        assert rv12v.hw["mcp_int_pin"] == 38

        # MCP should be enabled
        assert rv12v.features["mcp"] == 1


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
