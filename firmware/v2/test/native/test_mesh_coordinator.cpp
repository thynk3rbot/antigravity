/**
 * @file test_mesh_coordinator.cpp
 * @brief Unit tests for MeshCoordinator routing and neighbor management
 *
 * Stubs for Arduino/ESP32-specific APIs are provided before any includes
 * so the coordinator compiles and runs on native desktop.
 */

// ============================================================================
// Arduino / ESP32 Stubs (must come first, before any library includes)
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdarg>

// millis() stub — always returns 0; tests that require real time are skipped
static uint32_t _mock_millis_value = 0;
inline uint32_t millis() { return _mock_millis_value; }

// Minimal Serial stub
struct _SerialStub {
    void println(const char*) {}
    void printf(const char* fmt, ...) {
        (void)fmt;
    }
} Serial;

// Provide the Arduino header guard so mesh_coordinator.cpp doesn't re-include
#define ARDUINO_H

// ============================================================================
// Now include the modules under test
// ============================================================================

#include <unity.h>

// Pull in .cpp directly so we get the implementation without a build system
// dependency on the Arduino framework.
#include "../../lib/App/mesh_coordinator.cpp"  // NOLINT

// ============================================================================
// setUp / tearDown
// ============================================================================

void setUp() {
    MeshCoordinator::instance().clearNeighbors();
    MeshCoordinator::instance().clearStats();
    MeshCoordinator::instance().setOwnNodeID(1);
    _mock_millis_value = 0;
}

void tearDown() {}

// ============================================================================
// Tests: updateNeighbor / getNeighborCount
// ============================================================================

void test_updateNeighbor_adds_entry() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());
}

void test_updateNeighbor_two_distinct_nodes() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    mc.updateNeighbor(3, -80, 1);
    TEST_ASSERT_EQUAL(2, (int)mc.getNeighborCount());
}

void test_updateNeighbor_same_node_no_duplicate() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    mc.updateNeighbor(2, -65, 1);  // Update, not duplicate
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());
}

void test_updateNeighbor_ignores_self() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    // _ownNodeID is 1 (set in setUp)
    mc.updateNeighbor(1, -50, 1);
    TEST_ASSERT_EQUAL(0, (int)mc.getNeighborCount());
}

void test_updateNeighbor_ignores_broadcast_id() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(0xFF, -50, 1);
    TEST_ASSERT_EQUAL(0, (int)mc.getNeighborCount());
}

void test_updateNeighbor_updates_rssi() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    mc.updateNeighbor(2, -55, 1);
    const NeighborInfo* info = mc.getNeighbor(2);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_INT8(-55, info->rssi);
}

void test_getNeighbor_returns_null_for_unknown() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    TEST_ASSERT_NULL(mc.getNeighbor(99));
}

// ============================================================================
// Tests: getNextHop
// ============================================================================

void test_getNextHop_returns_best_rssi_neighbor() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -80, 1);
    mc.updateNeighbor(3, -60, 1);  // Better RSSI
    // Both hops==1; node 3 should win on RSSI
    uint8_t hop = mc.getNextHop(5);  // dest 5 unknown, picks best neighbor
    TEST_ASSERT_EQUAL_UINT8(3, hop);
}

void test_getNextHop_prefers_lower_hop_count() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -50, 2);  // 2 hops, better RSSI
    mc.updateNeighbor(3, -90, 1);  // 1 hop, worse RSSI
    uint8_t hop = mc.getNextHop(5);
    TEST_ASSERT_EQUAL_UINT8(3, hop);  // Lower hop count wins
}

void test_getNextHop_returns_0xFF_when_no_neighbors() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    TEST_ASSERT_EQUAL_UINT8(0xFF, mc.getNextHop(5));
}

void test_getNextHop_returns_0xFF_for_self() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -60, 1);
    // destID == ownNodeID (1) -> 0xFF per implementation
    TEST_ASSERT_EQUAL_UINT8(0xFF, mc.getNextHop(1));
}

// ============================================================================
// Tests: hasRoute
// ============================================================================

void test_hasRoute_true_for_known_neighbor() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(4, -70, 1);
    TEST_ASSERT_TRUE(mc.hasRoute(4));
}

void test_hasRoute_false_for_unknown() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    TEST_ASSERT_FALSE(mc.hasRoute(99));
}

void test_hasRoute_true_for_self() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    // ownNodeID==1; hasRoute(1) should be true
    TEST_ASSERT_TRUE(mc.hasRoute(1));
}

// ============================================================================
// Tests: shouldRelay
// ============================================================================

void test_shouldRelay_false_for_own_node_dest() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    ControlPacket pkt = ControlPacket::makeTelemetry(2, 1, 0, 0, 0, 0);
    // dest == ownNodeID (1)
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

void test_shouldRelay_false_for_broadcast_dest() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    ControlPacket pkt = ControlPacket::makeHeartbeat(2);  // dest == 0xFF
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

void test_shouldRelay_false_when_relay_flag_set() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);
    // Packet destined for node 5, but already has IS_RELAY flag
    ControlPacket pkt = ControlPacket::makeAction(2, 5, 0x01, 0x01);
    pkt.header.flags |= PKT_FLAG_IS_RELAY;
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

void test_shouldRelay_false_when_no_route() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    // No neighbor for dest 5
    ControlPacket pkt = ControlPacket::makeAction(2, 5, 0x01, 0x01);
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

void test_shouldRelay_true_when_route_exists_and_not_for_us() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);
    // Packet for node 5, not for us (ownNodeID==1), no relay flag
    ControlPacket pkt = ControlPacket::makeAction(2, 5, 0x01, 0x01);
    TEST_ASSERT_TRUE(mc.shouldRelay(pkt));
}

// ============================================================================
// Tests: ageOutNeighbors
// ============================================================================

void test_ageOutNeighbors_removes_stale_entry() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.setNeighborTimeout(1000);  // 1 second timeout

    _mock_millis_value = 0;
    mc.updateNeighbor(2, -70, 1);
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());

    // Advance time past timeout
    _mock_millis_value = 2000;
    mc.ageOutNeighbors();
    TEST_ASSERT_EQUAL(0, (int)mc.getNeighborCount());
}

void test_ageOutNeighbors_keeps_fresh_entry() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.setNeighborTimeout(5000);  // 5 second timeout

    _mock_millis_value = 0;
    mc.updateNeighbor(2, -70, 1);

    // Advance time, but less than timeout
    _mock_millis_value = 3000;
    mc.ageOutNeighbors();
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());
}

void test_ageOutNeighbors_removes_only_stale() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.setNeighborTimeout(1000);

    _mock_millis_value = 0;
    mc.updateNeighbor(2, -70, 1);   // Will become stale

    _mock_millis_value = 500;
    mc.updateNeighbor(3, -80, 1);   // Still fresh at eviction time

    _mock_millis_value = 1500;
    mc.ageOutNeighbors();

    // Node 2 stale (1500 - 0 = 1500 > 1000), node 3 fresh (1500 - 500 = 1000, not > 1000)
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());
    TEST_ASSERT_NOT_NULL(mc.getNeighbor(3));
    TEST_ASSERT_NULL(mc.getNeighbor(2));
}

// ============================================================================
// Tests: clearNeighbors / forgetNeighbor
// ============================================================================

void test_clearNeighbors_empties_table() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    mc.updateNeighbor(3, -80, 1);
    mc.clearNeighbors();
    TEST_ASSERT_EQUAL(0, (int)mc.getNeighborCount());
}

void test_forgetNeighbor_removes_specific() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 1);
    mc.updateNeighbor(3, -80, 1);
    mc.forgetNeighbor(2);
    TEST_ASSERT_EQUAL(1, (int)mc.getNeighborCount());
    TEST_ASSERT_NULL(mc.getNeighbor(2));
    TEST_ASSERT_NOT_NULL(mc.getNeighbor(3));
}

// ============================================================================
// Tests: getHopCount
// ============================================================================

void test_getHopCount_returns_0_for_self() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    TEST_ASSERT_EQUAL_UINT8(0, mc.getHopCount(1));
}

void test_getHopCount_returns_hopCount_for_known_neighbor() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(2, -70, 2);
    TEST_ASSERT_EQUAL_UINT8(2, mc.getHopCount(2));
}

void test_getHopCount_returns_0xFF_for_unknown() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    TEST_ASSERT_EQUAL_UINT8(0xFF, mc.getHopCount(99));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // updateNeighbor / getNeighborCount
    RUN_TEST(test_updateNeighbor_adds_entry);
    RUN_TEST(test_updateNeighbor_two_distinct_nodes);
    RUN_TEST(test_updateNeighbor_same_node_no_duplicate);
    RUN_TEST(test_updateNeighbor_ignores_self);
    RUN_TEST(test_updateNeighbor_ignores_broadcast_id);
    RUN_TEST(test_updateNeighbor_updates_rssi);
    RUN_TEST(test_getNeighbor_returns_null_for_unknown);

    // getNextHop
    RUN_TEST(test_getNextHop_returns_best_rssi_neighbor);
    RUN_TEST(test_getNextHop_prefers_lower_hop_count);
    RUN_TEST(test_getNextHop_returns_0xFF_when_no_neighbors);
    RUN_TEST(test_getNextHop_returns_0xFF_for_self);

    // hasRoute
    RUN_TEST(test_hasRoute_true_for_known_neighbor);
    RUN_TEST(test_hasRoute_false_for_unknown);
    RUN_TEST(test_hasRoute_true_for_self);

    // shouldRelay
    RUN_TEST(test_shouldRelay_false_for_own_node_dest);
    RUN_TEST(test_shouldRelay_false_for_broadcast_dest);
    RUN_TEST(test_shouldRelay_false_when_relay_flag_set);
    RUN_TEST(test_shouldRelay_false_when_no_route);
    RUN_TEST(test_shouldRelay_true_when_route_exists_and_not_for_us);

    // ageOutNeighbors
    RUN_TEST(test_ageOutNeighbors_removes_stale_entry);
    RUN_TEST(test_ageOutNeighbors_keeps_fresh_entry);
    RUN_TEST(test_ageOutNeighbors_removes_only_stale);

    // clearNeighbors / forgetNeighbor
    RUN_TEST(test_clearNeighbors_empties_table);
    RUN_TEST(test_forgetNeighbor_removes_specific);

    // getHopCount
    RUN_TEST(test_getHopCount_returns_0_for_self);
    RUN_TEST(test_getHopCount_returns_hopCount_for_known_neighbor);
    RUN_TEST(test_getHopCount_returns_0xFF_for_unknown);

    return UNITY_END();
}
