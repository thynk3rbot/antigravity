/*
  GossipManager: Peer-to-peer state dissemination for 1000s of devices.

  Pattern:
  - No central broker needed
  - Device A broadcasts status to 3 random neighbors
  - Each neighbor gossips to 3 more
  - Exponential spread → All peers know about updates within log(n) hops
  - Scales to 10,000+ devices

  Gossip message format:
  {
    "type": "gossip",
    "from": "DEV001",
    "version": "0.0.154",
    "battery_mv": 3400,
    "uptime_ms": 3600000,
    "timestamp": 1234567890
  }

  Transmitted via: LoRa ControlPacket (mesh relay)
*/

#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include <cstring>
#include "../HAL/board_config.h"

#define GOSSIP_MAX_PEERS 100
#define GOSSIP_BROADCAST_INTERVAL_MS 300000  // 5 minutes
#define GOSSIP_TTL 3                          // Hops before drop
#define GOSSIP_NEIGHBORS_TO_TELL 3            // Random peer selection

struct GossipPeer {
  char node_id[32];
  uint16_t battery_mv;
  uint32_t uptime_ms;
  uint32_t fw_version_packed;  // e.g., 0x000154 for 0.0.154
  uint32_t last_heard_ms;      // Timestamp of last gossip
  int8_t rssi;                 // Signal strength
};

struct GossipMessage {
  char from[32];
  uint16_t battery_mv;
  uint32_t uptime_ms;
  uint32_t fw_version_packed;
  uint32_t timestamp;
  uint8_t ttl;
};

class GossipManager {
 public:
  static GossipManager* getInstance();

  void init();
  void tick(uint32_t now_ms);

  // Local device status — broadcast this periodically
  void setLocalStatus(uint16_t battery_mv, uint32_t uptime_ms);

  // Receive gossip from neighbor — processes + relays
  void receiveGossip(const GossipMessage& msg);

  // Get peer info (network discovery)
  const std::vector<GossipPeer>& getPeers() const { return peers; }
  int getPeerCount() const { return peers.size(); }

  // Export peer list as JSON (for daemon sync)
  void exportPeersJSON(char* buffer, size_t buffer_len);

 private:
  GossipManager();

  static GossipManager* instance;

  std::vector<GossipPeer> peers;
  std::map<std::string, uint32_t> seen_messages;  // Deduplicate gossip

  uint16_t local_battery_mv = 0;
  uint32_t local_uptime_ms = 0;
  uint32_t last_broadcast_ms = 0;

  void broadcastGossip(uint32_t now_ms);
  void relayGossip(const GossipMessage& msg);
  std::vector<int> selectRandomPeers(int count);
  void updatePeerInfo(const GossipMessage& msg);
};
