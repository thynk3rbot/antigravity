#!/bin/bash
# Fleet test command script
# Usage: bash test_fleet.sh node-30 node-28 node-42

DAEMON_URL="http://localhost:8001"

echo "=================================="
echo "Phase 50 Fleet Test Commands"
echo "=================================="
echo ""

# Test 1: Get topology
echo "[1/5] Checking mesh topology..."
curl -s "$DAEMON_URL/api/mesh/topology" | jq '.node_count, .peers[] | {node_id, reachable, neighbors}'
echo ""

# Test 2: Send command to first device
if [ -z "$1" ]; then
    echo "ERROR: Please provide target node ID"
    echo "Usage: bash test_fleet.sh node-30 [node-28] [node-42]"
    exit 1
fi

TARGET=$1
echo "[2/5] Sending GPIO_TOGGLE to $TARGET..."
CMD_RESPONSE=$(curl -s -X POST "$DAEMON_URL/api/mesh/command" \
  -H "Content-Type: application/json" \
  -d "{
    \"target_node\": \"$TARGET\",
    \"action\": \"gpio_toggle\",
    \"pin\": 32,
    \"duration_ms\": 1000
  }")

CMD_ID=$(echo "$CMD_RESPONSE" | jq -r '.cmd_id')
echo "  Command ID: $CMD_ID"
echo "  Response: $CMD_RESPONSE" | jq .
echo ""

# Test 3: Wait for ACK
echo "[3/5] Waiting for device ACK (5 seconds)..."
sleep 5

echo "[4/5] Checking command status..."
curl -s "$DAEMON_URL/api/mesh/command/$CMD_ID" | jq .
echo ""

# Test 4: Get stats
echo "[5/5] Router statistics..."
curl -s "$DAEMON_URL/api/mesh/stats" | jq .
echo ""

echo "=================================="
echo "Test complete!"
echo "=================================="
