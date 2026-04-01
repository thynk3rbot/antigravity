/**
 * @file mesh_coordinator.h
 * @brief Multi-Hop Mesh Routing Coordinator
 *
 * Manages neighbor discovery, hop count tracking, and relay decisions
 * for Magic v2's multi-hop mesh network.
 * Uses greedy routing: always forward via neighbor with best RSSI + lowest hop count.
 */

#pragma once

#include "../Transport/interface.h"
#include "control_packet.h"
#include "mesh_config.h"
#include <map>
#include <stdint.h>
#include <functional>

// ============================================================================
// Neighbor Information
// ============================================================================

/**
 * @struct NeighborInfo
 * @brief Tracks a neighbor node in the mesh
 */
struct NeighborInfo {
  uint8_t nodeID;           // Neighbor's node ID
  int8_t rssi;              // Last seen RSSI (dBm)
  uint8_t hopCount;         // Hops to reach neighbor (1 = direct)
  uint32_t lastSeenMs;      // Timestamp of last packet (milliseconds)
  uint32_t packetCount;     // Packets received from this neighbor
};

// ============================================================================
// Mesh Coordinator Singleton
// ============================================================================

/**
 * @class MeshCoordinator
 * @brief Central mesh topology manager
 *
 * Responsibilities:
 * 1. Track neighbor nodes (RSSI, hop count, freshness)
 * 2. Determine if a packet should be relayed
 * 3. Select next hop for multi-hop delivery
 * 4. Age out stale neighbors (no activity for N seconds)
 * 5. Provide mesh diagnostics (neighbor table, relay count, etc.)
 */
class MeshCoordinator {
public:
  /**
   * @brief Get singleton instance
   * @return Reference to global MeshCoordinator
   */
  static MeshCoordinator& instance();

  /**
   * @brief Initialize mesh coordinator
   */
  void init();

  // ========================================================================
  // Neighbor Management
  // ========================================================================

  /**
   * @brief Update neighbor info from received packet
   * @param nodeID Neighbor's node ID
   * @param rssi Signal strength (dBm)
   * @param hopCount Hops from this node to neighbor
   */
  void updateNeighbor(uint8_t nodeID, int8_t rssi, uint8_t hopCount = 1);

  /**
   * @brief Get neighbor info
   * @param nodeID Node ID to look up
   * @return Pointer to NeighborInfo, or nullptr if not found
   */
  const NeighborInfo* getNeighbor(uint8_t nodeID) const;

  /**
   * @brief Get number of known neighbors
   * @return Neighbor count
   */
  size_t getNeighborCount() const { return _neighbors.size(); }

  /**
   * @brief Get reference to neighbor map
   * @return Reference to the private neighbor map
   */
  const std::map<uint8_t, NeighborInfo>& getNeighbors() const { return _neighbors; }

  /**
   * @brief Forget a neighbor (remove from table)
   * @param nodeID Node to remove
   */
  void forgetNeighbor(uint8_t nodeID);

  /**
   * @brief Age out stale neighbors
   * Removes neighbors not seen for more than _neighborTimeoutMs.
   * Called periodically (e.g., once per minute).
   */
  void ageOutNeighbors();

  /**
   * @brief Clear all neighbors
   */
  void clearNeighbors();

  // ========================================================================
  // Callbacks
  // ========================================================================
  
  /**
   * @brief Register callback invoked when a neighbor joins or leaves the mesh
   */
  using NeighborCallback = std::function<void(uint8_t, bool)>;
  void setNeighborCallback(NeighborCallback cb) { _neighborCallback = cb; }

  // ========================================================================
  // Discovery & V1 Compatibility
  // ========================================================================

  /**
   * @brief Check if node has discovered a master
   */
  bool isDiscovered() const { return _isDiscovered; }

  /**
   * @brief Mark node as discovered (Master reached)
   */
  void markDiscovered() { _isDiscovered = true; }

  /**
   * @brief Process a potential V1 legacy packet
   * @param buffer 64-byte raw buffer
   * @param len Buffer length
   * @return true if handled as V1
   */
  bool handleV1Packet(const uint8_t* buffer, size_t len);

  /**
   * @brief Periodic mesh activities (Discovery, Aging)
   */
  void poll();

  /**
   * @brief Determine if packet should be relayed
   * @param pkt Received packet
   * @return true if we should relay, false if destined for us or broadcast
   *
   * Relay conditions:
   * - Packet destination is not us
   * - Packet is not already relayed (prevent loops)
   * - Hop count < MAX_HOPS_ALLOWED
   * - We know a neighbor closer to destination
   */
  bool shouldRelay(const ControlPacket& pkt) const;

  /**
   * @brief Get optimal next hop to reach destination
   * @param destID Target node ID
   * @return Best neighbor node ID, or 0xFF if no route found
   *
   * Selection criteria:
   * 1. Must know a path to dest
   * 2. Choose lowest hop count
   * 3. If tied, choose best RSSI
   * 4. If still tied, choose lowest node ID
   */
  uint8_t getNextHop(uint8_t destID) const;

  /**
   * @brief Check if route to destination is available
   * @param destID Target node ID
   * @return true if we know a neighbor on the path to dest
   */
  bool hasRoute(uint8_t destID) const;

  /**
   * @brief Get hop count to destination
   * @param destID Target node ID
   * @return Hop count (0 if dest is us, 1 if direct, >1 if relayed)
   */
  uint8_t getHopCount(uint8_t destID) const;

  // ========================================================================
  // Diagnostics & Statistics
  // ========================================================================

  /**
   * @brief Get relay statistics
   * @return Total packets relayed since init
   */
  uint32_t getRelayCount() const { return _relayCount; }

  /**
   * @brief Get duplicate packets dropped
   * @return Count of packets prevented from re-routing
   */
  uint32_t getDroppedDuplicates() const { return _droppedDuplicates; }

  /**
   * @brief Get mesh status string
   * @return Human-readable status (neighbors, hops, etc.)
   */
  const char* getStatus() const;

  /**
   * @brief Get neighbor table as formatted string
   * @return Multi-line neighbor listing
   */
  const char* getNeighborTable() const;

  /**
   * @brief Clear statistics
   */
  void clearStats();

  // ========================================================================
  // Configuration
  // ========================================================================

  /**
   * @brief Set neighbor timeout
   * @param timeoutMs Milliseconds before neighbor is aged out
   */
  void setNeighborTimeout(uint32_t timeoutMs) { _neighborTimeoutMs = timeoutMs; }

  /**
   * @brief Set maximum hop count allowed
   * @param maxHops (default: 4)
   */
  void setMaxHops(uint8_t maxHops) { _maxHopsAllowed = maxHops; }

  /**
   * @brief Set our own node ID
   * @param id Our device's node ID (0 = Hub, 1-254 = Node)
   */
  void setOwnNodeID(uint8_t id) { _ownNodeID = id; }

private:
  // Private constructor for singleton
  MeshCoordinator() = default;

  // Neighbor registry (node ID -> NeighborInfo)
  std::map<uint8_t, NeighborInfo> _neighbors;

  // Configuration
  uint32_t _neighborTimeoutMs = 300000;  // 5 minutes default
  uint8_t _maxHopsAllowed = 4;
  uint8_t _ownNodeID = 255;              // Uninitialized

  // Discovery State
  bool     _isDiscovered = false;
  uint32_t _discoveryStartTime = 0;
  uint32_t _lastDiscoveryPing = 0;

  // Statistics
  uint32_t _relayCount = 0;
  uint32_t _droppedDuplicates = 0;

  // Deduplication: track recently relayed packets to prevent loops
  uint32_t _relayedSeqNumbers[16];       // Rolling buffer
  uint8_t _relayedIndex = 0;

  // Status buffer (for getStatus())
  mutable char _statusBuffer[256];

  // Helper: check if seq number was recently relayed
  bool _wasRecentlyRelayed(uint8_t srcID, uint8_t seqNum) const;

  // Helper: mark seq number as relayed
  void _markAsRelayed(uint8_t srcID, uint8_t seqNum);

  // Status callback
  NeighborCallback _neighborCallback;
};

// ============================================================================
// Global Instance Access
// ============================================================================

extern MeshCoordinator& meshCoordinator;

inline MeshCoordinator& getMeshCoordinator() {
  return MeshCoordinator::instance();
}
