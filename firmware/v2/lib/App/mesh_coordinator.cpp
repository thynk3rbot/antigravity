/**
 * @file mesh_coordinator.cpp
 * @brief Mesh Coordinator Implementation
 */

#include "mesh_coordinator.h"
#include <Arduino.h>
#include <cstring>
#include <vector>
#include "power_manager.h"
#include "../Transport/lora_transport.h"
#include "../Transport/espnow_transport.h"

// Static instance
MeshCoordinator& meshCoordinator = MeshCoordinator::instance();

// ============================================================================
// Singleton Implementation
// ============================================================================

MeshCoordinator& MeshCoordinator::instance() {
  static MeshCoordinator _instance;
  return _instance;
}

void MeshCoordinator::init() {
  _neighbors.clear();
  memset(_relayedSeqNumbers, 0, sizeof(_relayedSeqNumbers));
  _relayedIndex = 0;
  _relayCount = 0;
  _droppedDuplicates = 0;

  _isDiscovered = false;
  _discoveryStartTime = millis();
  _lastDiscoveryPing = 0;

  Serial.println("[MeshCoordinator] Initialized");
}

void MeshCoordinator::poll() {
  ageOutNeighbors();

  uint32_t now = millis();
  
  // Wait before first discovery ping to listen for existing nodes
  if (now < _discoveryStartTime + BOOT_SILENCE_MS) {
    return;
  }

  uint32_t interval = DISCOVERY_INTERVAL_S * 1000;

  if (PowerManager::isPowered()) {
    interval = USB_HEART_BEAT_S * 1000;
  } else if (!_isDiscovered && (now - _discoveryStartTime < DISCOVERY_BURST_MS)) {
    // If we've already found the Hub (Node 0) from an incoming packet, we can slow down immediately
    if (_neighbors.count(0) > 0) {
      interval = NORMAL_HEART_BEAT_S * 1000;
    } else {
      interval = DISCOVERY_INTERVAL_S * 1000;
    }
  } else {
    interval = NORMAL_HEART_BEAT_S * 1000;
  }

  // Add random jitter to prevent synchronized collisions (up to 500ms)
  static uint32_t jitter = random(500); 

  if (now - _lastDiscoveryPing > interval + jitter) {
    // Send discovery heartbeats on all transports
    ControlPacket pkt = ControlPacket::makeHeartbeat(_ownNodeID);
    loraTransport.send((uint8_t*)&pkt, sizeof(pkt));
    espNowTransport.send((uint8_t*)&pkt, sizeof(pkt));
    
    _lastDiscoveryPing = now;
    jitter = random(500); // Re-roll jitter for next ping
    Serial.printf("[MeshCoordinator] Heartbeat sent (Interval: %lu ms, Jitter: %lu ms)\n", interval, jitter);
  }
}

bool MeshCoordinator::handleV1Packet(const uint8_t* buffer, size_t len) {
  if (len < 1 || buffer[0] != V1_BINARY_TOKEN) {
    return false;
  }

  // V1 Token detected! (0xAA)
  Serial.printf("[MeshCoordinator] Legacy V1 Packet (0x%02X) detected\n", buffer[0]);
  
  // TODO: Implement full mapping to V1BinaryCmd
  // For now, if we see 0xAA from Master, mark as discovered
  // (In V1, Master usually ACKs with 0xAA 0x00 0x08 ...)
  
  return true;
}

// ============================================================================
// Neighbor Management
// ============================================================================

void MeshCoordinator::updateNeighbor(uint8_t nodeID, int8_t rssi,
                                      uint8_t hopCount) {
  if (nodeID == _ownNodeID || nodeID == 0xFF) {
    return;  // Don't track self or broadcast
  }

  uint32_t now = millis();

  auto it = _neighbors.find(nodeID);
  if (it != _neighbors.end()) {
    // Update existing neighbor
    it->second.rssi = rssi;
    it->second.hopCount = hopCount;
    it->second.lastSeenMs = now;
    it->second.packetCount++;
  } else {
    // Add new neighbor
    NeighborInfo info;
    info.nodeID = nodeID;
    info.rssi = rssi;
    info.hopCount = hopCount;
    info.lastSeenMs = now;
    info.packetCount = 1;

    _neighbors[nodeID] = info;
    if (_neighborCallback) _neighborCallback(nodeID, true);
    Serial.printf("[MeshCoordinator] New neighbor: Node %u (RSSI %d dBm, %u hops)\n",
                  nodeID, rssi, hopCount);
  }
}

const NeighborInfo* MeshCoordinator::getNeighbor(uint8_t nodeID) const {
  auto it = _neighbors.find(nodeID);
  if (it != _neighbors.end()) {
    return &it->second;
  }
  return nullptr;
}

void MeshCoordinator::forgetNeighbor(uint8_t nodeID) {
  auto it = _neighbors.find(nodeID);
  if (it != _neighbors.end()) {
    _neighbors.erase(it);
    if (_neighborCallback) _neighborCallback(nodeID, false);
    Serial.printf("[MeshCoordinator] Forgot neighbor: Node %u\n", nodeID);
  }
}

void MeshCoordinator::ageOutNeighbors() {
  uint32_t now = millis();
  std::vector<uint8_t> toRemove;

  for (auto& pair : _neighbors) {
    if (now - pair.second.lastSeenMs > _neighborTimeoutMs) {
      toRemove.push_back(pair.first);
    }
  }

  for (uint8_t nodeID : toRemove) {
    forgetNeighbor(nodeID);
  }

  if (toRemove.size() > 0) {
    Serial.printf("[MeshCoordinator] Aged out %zu stale neighbors\n",
                  toRemove.size());
  }
}

void MeshCoordinator::clearNeighbors() {
  _neighbors.clear();
  Serial.println("[MeshCoordinator] All neighbors cleared");
}

// ============================================================================
// Relay Logic
// ============================================================================

bool MeshCoordinator::shouldRelay(const ControlPacket& pkt) const {
  // Don't relay if packet is for us
  if (pkt.header.dest == _ownNodeID || pkt.header.dest == 0xFF) {
    return false;
  }

  // Don't relay if already relayed (prevent loops)
  if (pkt.header.flags & PKT_FLAG_IS_RELAY) {
    return false;
  }

  // Check if we know a route to destination
  if (!hasRoute(pkt.header.dest)) {
    return false;
  }

  return true;
}

uint8_t MeshCoordinator::getNextHop(uint8_t destID) const {
  if (destID == _ownNodeID || destID == 0xFF) {
    return 0xFF;
  }

  uint8_t bestNeighbor = 0xFF;
  uint8_t bestHops = 0xFF;
  int8_t bestRSSI = -127;

  for (const auto& pair : _neighbors) {
    const NeighborInfo& neighbor = pair.second;

    // Skip if this neighbor is farther than max hops
    if (neighbor.hopCount >= _maxHopsAllowed) {
      continue;
    }

    // Prefer lower hop count
    if (neighbor.hopCount < bestHops ||
        (neighbor.hopCount == bestHops && neighbor.rssi > bestRSSI)) {
      bestNeighbor = neighbor.nodeID;
      bestHops = neighbor.hopCount;
      bestRSSI = neighbor.rssi;
    }
  }

  return bestNeighbor;
}

bool MeshCoordinator::hasRoute(uint8_t destID) const {
  if (destID == _ownNodeID) {
    return true;  // We are the destination
  }

  // Check if we have a direct neighbor (simple case)
  return _neighbors.find(destID) != _neighbors.end();
}

uint8_t MeshCoordinator::getHopCount(uint8_t destID) const {
  if (destID == _ownNodeID) {
    return 0;
  }

  auto it = _neighbors.find(destID);
  if (it != _neighbors.end()) {
    return it->second.hopCount;
  }

  return 0xFF;  // Unknown
}

// ============================================================================
// Diagnostics
// ============================================================================

const char* MeshCoordinator::getStatus() const {
  snprintf(_statusBuffer, sizeof(_statusBuffer),
           "Mesh: %zu neighbors, %lu relayed, %lu dropped",
           _neighbors.size(), _relayCount, _droppedDuplicates);
  return _statusBuffer;
}

const char* MeshCoordinator::getNeighborTable() const {
  static char buffer[512];
  char* ptr = buffer;
  int remaining = sizeof(buffer);

  snprintf(ptr, remaining, "=== Neighbor Table ===\n");
  ptr += strlen(ptr);
  remaining = sizeof(buffer) - (ptr - buffer);

  for (const auto& pair : _neighbors) {
    const NeighborInfo& neighbor = pair.second;
    int len = snprintf(ptr, remaining,
                       "Node %u: RSSI=%d dBm, Hops=%u, Pkts=%lu\n",
                       neighbor.nodeID, neighbor.rssi, neighbor.hopCount,
                       neighbor.packetCount);
    ptr += len;
    remaining -= len;
    if (remaining <= 0) break;
  }

  return buffer;
}

void MeshCoordinator::clearStats() {
  _relayCount = 0;
  _droppedDuplicates = 0;
  Serial.println("[MeshCoordinator] Statistics cleared");
}

// ============================================================================
// Private Helpers
// ============================================================================

bool MeshCoordinator::_wasRecentlyRelayed(uint8_t srcID, uint8_t seqNum) const {
  uint32_t key = (srcID << 8) | seqNum;

  for (int i = 0; i < 16; i++) {
    if (_relayedSeqNumbers[i] == key) {
      return true;
    }
  }

  return false;
}

void MeshCoordinator::_markAsRelayed(uint8_t srcID, uint8_t seqNum) {
  uint32_t key = (srcID << 8) | seqNum;
  _relayedSeqNumbers[_relayedIndex] = key;
  _relayedIndex = (_relayedIndex + 1) % 16;
}
