/*
  GossipManager: Peer-to-peer state dissemination (implementation)
*/

#include "GossipManager.h"
#include <algorithm>
#include <random>
#include <cstdio>

GossipManager* GossipManager::instance = nullptr;

GossipManager* GossipManager::getInstance() {
  if (!instance) {
    instance = new GossipManager();
  }
  return instance;
}

GossipManager::GossipManager() {
  peers.reserve(GOSSIP_MAX_PEERS);
}

void GossipManager::init() {
  Serial.println("[Gossip] Initialized. Will broadcast status every 5 minutes.");
  last_broadcast_ms = millis();
}

void GossipManager::tick(uint32_t now_ms) {
  // Periodically broadcast local status
  if (now_ms - last_broadcast_ms > GOSSIP_BROADCAST_INTERVAL_MS) {
    broadcastGossip(now_ms);
    last_broadcast_ms = now_ms;
  }

  // Mark stale peers as offline (remove if no gossip for 15 min)
  for (auto it = peers.begin(); it != peers.end();) {
    if (now_ms - it->last_heard_ms > 900000) {  // 15 minutes
      Serial.printf("[Gossip] Removing stale peer: %s\n", it->node_id);
      it = peers.erase(it);
    } else {
      ++it;
    }
  }
}

void GossipManager::setLocalStatus(uint16_t battery_mv, uint32_t uptime_ms) {
  local_battery_mv = battery_mv;
  local_uptime_ms = uptime_ms;
}

void GossipManager::broadcastGossip(uint32_t now_ms) {
  GossipMessage msg;
  strncpy(msg.from, FIRMWARE_DEVICE_ID, sizeof(msg.from) - 1);
  msg.battery_mv = local_battery_mv;
  msg.uptime_ms = local_uptime_ms;
  msg.fw_version_packed = FIRMWARE_VERSION_PACKED;
  msg.timestamp = now_ms / 1000;  // Seconds
  msg.ttl = GOSSIP_TTL;

  // Select 3 random peers to tell
  std::vector<int> neighbors = selectRandomPeers(GOSSIP_NEIGHBORS_TO_TELL);

  Serial.printf("[Gossip] Broadcasting to %lu peers\n", neighbors.size());

  // In real implementation: Send msg via ControlPacket to each neighbor
  // This is a placeholder — actual transmission would use MeshRouter
  for (int idx : neighbors) {
    if (idx < (int)peers.size()) {
      Serial.printf(
        "[Gossip] → %s (battery: %dmV, uptime: %lums)\n",
        peers[idx].node_id,
        msg.battery_mv,
        msg.uptime_ms
      );
    }
  }
}

void GossipManager::receiveGossip(const GossipMessage& msg) {
  // Check if we've seen this message already (TTL + from + timestamp)
  char msg_key[128];
  snprintf(msg_key, sizeof(msg_key), "%s:%u:%u", msg.from, msg.timestamp, msg.ttl);

  if (seen_messages.count(msg_key)) {
    // Already processed, don't relay again
    return;
  }

  // Mark as seen
  seen_messages[msg_key] = millis();

  // Update peer info
  updatePeerInfo(msg);

  // Relay to 3 random neighbors if TTL > 0
  if (msg.ttl > 0) {
    GossipMessage relay_msg = msg;
    relay_msg.ttl--;
    relayGossip(relay_msg);
  }
}

void GossipManager::updatePeerInfo(const GossipMessage& msg) {
  // Find existing peer or add new
  GossipPeer* peer = nullptr;

  for (auto& p : peers) {
    if (strncmp(p.node_id, msg.from, sizeof(p.node_id)) == 0) {
      peer = &p;
      break;
    }
  }

  if (!peer && peers.size() < GOSSIP_MAX_PEERS) {
    peers.push_back({});
    peer = &peers.back();
    strncpy(peer->node_id, msg.from, sizeof(peer->node_id) - 1);
    Serial.printf("[Gossip] New peer discovered: %s\n", msg.from);
  }

  if (peer) {
    peer->battery_mv = msg.battery_mv;
    peer->uptime_ms = msg.uptime_ms;
    peer->fw_version_packed = msg.fw_version_packed;
    peer->last_heard_ms = millis();
  }
}

void GossipManager::relayGossip(const GossipMessage& msg) {
  // Select 3 random peers to relay to
  std::vector<int> neighbors = selectRandomPeers(GOSSIP_NEIGHBORS_TO_TELL);

  Serial.printf("[Gossip] Relaying from %s (TTL: %u) to %lu peers\n",
               msg.from, msg.ttl, neighbors.size());

  // In real implementation: Send relay_msg via ControlPacket
  // This is placeholder code
  for (int idx : neighbors) {
    if (idx < (int)peers.size()) {
      Serial.printf("[Gossip] ↻ %s\n", peers[idx].node_id);
    }
  }
}

std::vector<int> GossipManager::selectRandomPeers(int count) {
  std::vector<int> result;

  if (peers.empty()) {
    return result;
  }

  // Simple random selection without std::random (for Arduino)
  count = std::min(count, (int)peers.size());

  for (int i = 0; i < count; i++) {
    int idx = random(0, peers.size());
    result.push_back(idx);
  }

  return result;
}

void GossipManager::exportPeersJSON(char* buffer, size_t buffer_len) {
  // Export peer list as JSON for daemon consumption
  // Example: {"peers":[{"node_id":"DEV001","battery_mv":3400,...}]}

  size_t pos = 0;
  pos += snprintf(buffer + pos, buffer_len - pos, "{\"peers\":[");

  for (size_t i = 0; i < peers.size() && pos < buffer_len - 100; i++) {
    if (i > 0) {
      pos += snprintf(buffer + pos, buffer_len - pos, ",");
    }

    pos += snprintf(
      buffer + pos,
      buffer_len - pos,
      "{\"node_id\":\"%s\",\"battery_mv\":%u,\"uptime_ms\":%lu,\"fw_ver\":\"0.0.%u\"}",
      peers[i].node_id,
      peers[i].battery_mv,
      peers[i].uptime_ms,
      peers[i].fw_version_packed & 0xFF  // Last digit is hardware class
    );
  }

  pos += snprintf(buffer + pos, buffer_len - pos, "]}");
}
