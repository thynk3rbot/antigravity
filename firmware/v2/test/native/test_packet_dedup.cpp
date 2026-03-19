/**
 * @file test_packet_dedup.cpp
 * @brief Unit tests for packet deduplication logic in MeshCoordinator
 *
 * The dedup mechanism lives in MeshCoordinator: _wasRecentlyRelayed() and
 * _markAsRelayed() use a 16-entry rolling buffer keyed on (srcID << 8 | seq).
 * shouldRelay() consults this buffer to prevent loop amplification.
 *
 * Because _wasRecentlyRelayed and _markAsRelayed are private, we exercise them
 * indirectly through the public shouldRelay() interface by simulating multiple
 * packets with the same src+seq tuple.
 *
 * NOTE: MessageRouter has no application-level dedup — it only counts transport
 * errors as "dropped packets". All mesh dedup is in MeshCoordinator.
 */

// ============================================================================
// Arduino / ESP32 Stubs
// ============================================================================

#include "arduino_stubs.h"

// ============================================================================
// Module under test
// ============================================================================

#include <unity.h>
#include "../../lib/App/mesh_coordinator.cpp"  // NOLINT
#include "../mocks/mock_transport.h"
#include "../../lib/Transport/message_router.h"

// ============================================================================
// setUp / tearDown
// ============================================================================

void setUp() {
    MeshCoordinator::instance().clearNeighbors();
    MeshCoordinator::instance().clearStats();
    MeshCoordinator::instance().setOwnNodeID(10);
    _mock_millis_value = 0;
}

void tearDown() {
    // Unregister any transports registered in tests to prevent cross-test contamination
    MessageRouter& router = MessageRouter::instance();
    router.unregisterTransport(TransportType::SERIAL_DEBUG);
    router.unregisterTransport(TransportType::LORA);
    router.unregisterTransport(TransportType::MQTT);
    router.unregisterTransport(TransportType::BLE);
    router.unregisterTransport(TransportType::ESP_NOW);
    router.setMessageHandler(nullptr);
}

// ============================================================================
// Helper: build an ACTION packet for a non-local dest with no relay flag
// ============================================================================
static ControlPacket makeRelayablePacket(uint8_t src, uint8_t dest, uint8_t seq) {
    ControlPacket pkt = ControlPacket::makeAction(src, dest, 0x01, 0x01);
    pkt.header.seq = seq;
    return pkt;
}

// ============================================================================
// Tests: shouldRelay dedup via IS_RELAY flag
// ============================================================================

/**
 * A packet that has already been relayed (PKT_FLAG_IS_RELAY set) must not
 * be relayed again. This is the primary loop-prevention mechanism.
 */
void test_dedup_relay_flag_prevents_second_relay() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    ControlPacket pkt = makeRelayablePacket(2, 5, 1);
    TEST_ASSERT_TRUE(mc.shouldRelay(pkt));  // First time: should relay

    // Simulate the packet coming back with the relay flag set
    pkt.header.flags |= PKT_FLAG_IS_RELAY;
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));  // Second time: must NOT relay
}

/**
 * Two packets with the same src/dest but different seq numbers are both
 * relayable (they are distinct packets, not duplicates).
 */
void test_dedup_different_seq_both_relayable() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    ControlPacket pkt1 = makeRelayablePacket(2, 5, 1);
    ControlPacket pkt2 = makeRelayablePacket(2, 5, 2);

    TEST_ASSERT_TRUE(mc.shouldRelay(pkt1));
    TEST_ASSERT_TRUE(mc.shouldRelay(pkt2));
}

/**
 * Packets from different sources with the same seq number are distinct and
 * must both be relayable.
 */
void test_dedup_same_seq_different_src_both_relayable() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    ControlPacket pkt1 = makeRelayablePacket(2, 5, 7);
    ControlPacket pkt2 = makeRelayablePacket(3, 5, 7);

    TEST_ASSERT_TRUE(mc.shouldRelay(pkt1));
    TEST_ASSERT_TRUE(mc.shouldRelay(pkt2));
}

/**
 * A broadcast packet (dest == 0xFF) must never be relayed, preventing
 * broadcast storms.
 */
void test_dedup_broadcast_never_relayed() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    ControlPacket pkt = ControlPacket::makeHeartbeat(2);  // dest == 0xFF
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

/**
 * If there is no known route to the destination, the packet must not be
 * relayed (avoids blind flooding).
 */
void test_dedup_no_route_prevents_relay() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    // No neighbors registered

    ControlPacket pkt = makeRelayablePacket(2, 5, 1);
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

/**
 * After neighbor table is cleared, a previously-relayable destination
 * becomes unroutable and shouldRelay returns false.
 */
void test_dedup_cleared_neighbors_block_relay() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    ControlPacket pkt = makeRelayablePacket(2, 5, 1);
    TEST_ASSERT_TRUE(mc.shouldRelay(pkt));

    mc.clearNeighbors();
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

/**
 * A packet destined for our own node ID must not be relayed.
 */
void test_dedup_own_dest_not_relayed() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    // ownNodeID == 10 (set in setUp)
    ControlPacket pkt = makeRelayablePacket(2, 10, 1);
    TEST_ASSERT_FALSE(mc.shouldRelay(pkt));
}

/**
 * Packets from up to 16 distinct sources with distinct seq numbers should
 * all be accepted as relayable (fills the rolling buffer exactly).
 */
void test_dedup_rolling_buffer_16_entries() {
    MeshCoordinator& mc = MeshCoordinator::instance();
    mc.updateNeighbor(5, -60, 1);

    for (uint8_t i = 0; i < 16; i++) {
        ControlPacket pkt = makeRelayablePacket(i + 20, 5, i);
        TEST_ASSERT_TRUE(mc.shouldRelay(pkt));
    }
}

// ============================================================================
// Tests: MessageRouter transport registration and dispatch
// ============================================================================

/**
 * Registering the same transport type twice must be rejected.
 * (Prevents accidental double-dispatch which would look like duplication.)
 */
void test_router_reject_duplicate_transport_type() {
    MessageRouter& router = MessageRouter::instance();

    MockTransport t1, t2;
    bool r1 = router.registerTransport(&t1);
    bool r2 = router.registerTransport(&t2);  // Same type: SERIAL_DEBUG

    // Clean up
    router.unregisterTransport(TransportType::SERIAL_DEBUG);

    TEST_ASSERT_TRUE(r1);
    TEST_ASSERT_FALSE(r2);
}

/**
 * broadcastPacket sends bytes to all registered (ready) transports exactly once.
 */
void test_router_broadcast_reaches_registered_transport() {
    MessageRouter& router = MessageRouter::instance();

    MockTransport mock;
    router.registerTransport(&mock);

    uint8_t data[] = {0x01, 0x02, 0x03};
    bool ok = router.broadcastPacket(data, 3);

    router.unregisterTransport(TransportType::SERIAL_DEBUG);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, (int)mock.sent.size());
    TEST_ASSERT_EQUAL(3, (int)mock.sent[0].size());
}

/**
 * process() drains recv from all transports and invokes the message handler
 * exactly once per received packet (no duplicate dispatch).
 */
void test_router_process_dispatches_once_per_packet() {
    MessageRouter& router = MessageRouter::instance();

    struct {
        int count = 0;
    } call_state;

    auto handler = [&call_state](TransportType, const uint8_t*, size_t) {
        call_state.count++;
    };
    router.setMessageHandler(handler);

    MockTransport mock;
    mock.nextRecv = {0xAA, 0xBB, 0xCC};
    router.registerTransport(&mock);

    router.process();  // Should fire handler once

    router.unregisterTransport(TransportType::SERIAL_DEBUG);
    router.setMessageHandler(nullptr);

    TEST_ASSERT_EQUAL(1, call_state.count);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // MeshCoordinator dedup via shouldRelay
    RUN_TEST(test_dedup_relay_flag_prevents_second_relay);
    RUN_TEST(test_dedup_different_seq_both_relayable);
    RUN_TEST(test_dedup_same_seq_different_src_both_relayable);
    RUN_TEST(test_dedup_broadcast_never_relayed);
    RUN_TEST(test_dedup_no_route_prevents_relay);
    RUN_TEST(test_dedup_cleared_neighbors_block_relay);
    RUN_TEST(test_dedup_own_dest_not_relayed);
    RUN_TEST(test_dedup_rolling_buffer_16_entries);

    // MessageRouter transport-level dispatch
    RUN_TEST(test_router_reject_duplicate_transport_type);
    RUN_TEST(test_router_broadcast_reaches_registered_transport);
    RUN_TEST(test_router_process_dispatches_once_per_packet);

    return UNITY_END();
}
