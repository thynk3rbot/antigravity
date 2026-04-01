/**
 * @file test_control_packet.cpp
 * @brief Unit tests for ControlPacket struct and factory methods
 *
 * Tests run natively (no hardware). Covers struct layout, factory methods,
 * and field correctness for all packet types.
 */

#include <unity.h>
#include "../lib/App/control_packet.h"

// ---------------------------------------------------------------------------
// setUp / tearDown (required by Unity/PlatformIO test runner)
// ---------------------------------------------------------------------------

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Tests: Struct size
// ---------------------------------------------------------------------------

void test_sizeof_ControlPacket_is_14() {
    TEST_ASSERT_EQUAL(14, (int)sizeof(ControlPacket));
}

void test_sizeof_PacketHeader_is_6() {
    TEST_ASSERT_EQUAL(6, (int)sizeof(PacketHeader));
}

void test_sizeof_TelemetryPayload_is_8() {
    TEST_ASSERT_EQUAL(8, (int)sizeof(TelemetryPayload));
}

void test_sizeof_ActionPayload_is_2() {
    TEST_ASSERT_EQUAL(2, (int)sizeof(ActionPayload));
}

// ---------------------------------------------------------------------------
// Tests: makeTelemetry
// ---------------------------------------------------------------------------

void test_makeTelemetry_type() {
    ControlPacket pkt = ControlPacket::makeTelemetry(1, 0, 250, 330, 0x03, 80);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(PacketType::TELEMETRY),
        pkt.header.type
    );
}

void test_makeTelemetry_src_dest() {
    ControlPacket pkt = ControlPacket::makeTelemetry(5, 0, 250, 330, 0x00, 80);
    TEST_ASSERT_EQUAL_UINT8(5,   pkt.header.src);
    TEST_ASSERT_EQUAL_UINT8(0,   pkt.header.dest);
}

void test_makeTelemetry_payload_fields() {
    ControlPacket pkt = ControlPacket::makeTelemetry(1, 0xFF, 256, 500, 0xAB, 120);
    TEST_ASSERT_EQUAL_UINT16(256,  pkt.payload.telemetry.tempC_x10);
    TEST_ASSERT_EQUAL_UINT16(500,  pkt.payload.telemetry.voltageV_x100);
    TEST_ASSERT_EQUAL_UINT8(0xAB,  pkt.payload.telemetry.relayState);
    TEST_ASSERT_EQUAL_UINT8(120,   pkt.payload.telemetry.rssi);
}

void test_makeTelemetry_uptime_initialized_zero() {
    ControlPacket pkt = ControlPacket::makeTelemetry(1, 0, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(0, pkt.payload.telemetry.uptime_min);
}

void test_makeTelemetry_seq_initialized_zero() {
    ControlPacket pkt = ControlPacket::makeTelemetry(2, 0, 100, 320, 0, 60);
    TEST_ASSERT_EQUAL_UINT8(0, pkt.header.seq);
}

// ---------------------------------------------------------------------------
// Tests: makeAction
// ---------------------------------------------------------------------------

void test_makeAction_type() {
    ControlPacket pkt = ControlPacket::makeAction(0, 3, 0x01, 0x01);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(PacketType::ACTION),
        pkt.header.type
    );
}

void test_makeAction_src_dest() {
    ControlPacket pkt = ControlPacket::makeAction(0, 7, 0x0F, 0x0F);
    TEST_ASSERT_EQUAL_UINT8(0, pkt.header.src);
    TEST_ASSERT_EQUAL_UINT8(7, pkt.header.dest);
}

void test_makeAction_relayMask() {
    ControlPacket pkt = ControlPacket::makeAction(0, 2, 0xAA, 0x55);
    TEST_ASSERT_EQUAL_UINT8(0xAA, pkt.payload.action.relayMask);
    TEST_ASSERT_EQUAL_UINT8(0x55, pkt.payload.action.relayState);
}

void test_makeAction_requires_ack_flag() {
    ControlPacket pkt = ControlPacket::makeAction(0, 1, 0x01, 0x01);
    TEST_ASSERT_TRUE(pkt.header.requiresACK());
}

// ---------------------------------------------------------------------------
// Tests: makeACK
// ---------------------------------------------------------------------------

void test_makeACK_type() {
    ControlPacket pkt = ControlPacket::makeACK(1, 0, 42);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(PacketType::ACK),
        pkt.header.type
    );
}

void test_makeACK_seq() {
    ControlPacket pkt = ControlPacket::makeACK(1, 0, 42);
    TEST_ASSERT_EQUAL_UINT8(42, pkt.header.seq);
}

void test_makeACK_src_dest() {
    ControlPacket pkt = ControlPacket::makeACK(3, 7, 10);
    TEST_ASSERT_EQUAL_UINT8(3, pkt.header.src);
    TEST_ASSERT_EQUAL_UINT8(7, pkt.header.dest);
}

void test_makeACK_no_require_ack_flag() {
    ControlPacket pkt = ControlPacket::makeACK(1, 0, 1);
    TEST_ASSERT_FALSE(pkt.header.requiresACK());
}

void test_makeACK_payload_zeroed() {
    ControlPacket pkt = ControlPacket::makeACK(1, 0, 5);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, pkt.payload.raw[i]);
    }
}

// ---------------------------------------------------------------------------
// Tests: makeHeartbeat
// ---------------------------------------------------------------------------

void test_makeHeartbeat_type() {
    ControlPacket pkt = ControlPacket::makeHeartbeat(2);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(PacketType::HEARTBEAT),
        pkt.header.type
    );
}

void test_makeHeartbeat_src() {
    ControlPacket pkt = ControlPacket::makeHeartbeat(9);
    TEST_ASSERT_EQUAL_UINT8(9, pkt.header.src);
}

void test_makeHeartbeat_dest_broadcast() {
    ControlPacket pkt = ControlPacket::makeHeartbeat(1);
    TEST_ASSERT_EQUAL_UINT8(0xFF, pkt.header.dest);
    TEST_ASSERT_TRUE(pkt.header.isBroadcast());
}

void test_makeHeartbeat_payload_zeroed() {
    ControlPacket pkt = ControlPacket::makeHeartbeat(1);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, pkt.payload.raw[i]);
    }
}

// ---------------------------------------------------------------------------
// Tests: PacketHeader helpers
// ---------------------------------------------------------------------------

void test_header_requiresACK_flag_set() {
    PacketHeader hdr;
    hdr.flags = PKT_FLAG_REQUIRE_ACK;
    TEST_ASSERT_TRUE(hdr.requiresACK());
}

void test_header_requiresACK_flag_clear() {
    PacketHeader hdr;
    hdr.flags = 0;
    TEST_ASSERT_FALSE(hdr.requiresACK());
}

void test_header_isRelayed_flag_set() {
    PacketHeader hdr;
    hdr.flags = PKT_FLAG_IS_RELAY;
    TEST_ASSERT_TRUE(hdr.isRelayed());
}

void test_header_isBroadcast_dest_255() {
    PacketHeader hdr;
    hdr.dest = 255;
    TEST_ASSERT_TRUE(hdr.isBroadcast());
}

void test_header_isBroadcast_dest_not_255() {
    PacketHeader hdr;
    hdr.dest = 1;
    TEST_ASSERT_FALSE(hdr.isBroadcast());
}

// ---------------------------------------------------------------------------
// Tests: rssi utility functions
// ---------------------------------------------------------------------------

void test_rssiByteToDbm_zero() {
    // 0 -> -120 + 0/2 = -120
    TEST_ASSERT_EQUAL_INT8(-120, rssiByteToDbm(0));
}

void test_rssiDbmToByte_round_trip() {
    // Convert -80 dBm to byte and back
    uint8_t b = rssiDbmToByte(-80);
    int8_t back = rssiByteToDbm(b);
    // Allow ±1 for rounding
    TEST_ASSERT_INT8_WITHIN(1, -80, back);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Struct sizes
    RUN_TEST(test_sizeof_ControlPacket_is_14);
    RUN_TEST(test_sizeof_PacketHeader_is_6);
    RUN_TEST(test_sizeof_TelemetryPayload_is_8);
    RUN_TEST(test_sizeof_ActionPayload_is_2);

    // makeTelemetry
    RUN_TEST(test_makeTelemetry_type);
    RUN_TEST(test_makeTelemetry_src_dest);
    RUN_TEST(test_makeTelemetry_payload_fields);
    RUN_TEST(test_makeTelemetry_uptime_initialized_zero);
    RUN_TEST(test_makeTelemetry_seq_initialized_zero);

    // makeAction
    RUN_TEST(test_makeAction_type);
    RUN_TEST(test_makeAction_src_dest);
    RUN_TEST(test_makeAction_relayMask);
    RUN_TEST(test_makeAction_requires_ack_flag);

    // makeACK
    RUN_TEST(test_makeACK_type);
    RUN_TEST(test_makeACK_seq);
    RUN_TEST(test_makeACK_src_dest);
    RUN_TEST(test_makeACK_no_require_ack_flag);
    RUN_TEST(test_makeACK_payload_zeroed);

    // makeHeartbeat
    RUN_TEST(test_makeHeartbeat_type);
    RUN_TEST(test_makeHeartbeat_src);
    RUN_TEST(test_makeHeartbeat_dest_broadcast);
    RUN_TEST(test_makeHeartbeat_payload_zeroed);

    // PacketHeader helpers
    RUN_TEST(test_header_requiresACK_flag_set);
    RUN_TEST(test_header_requiresACK_flag_clear);
    RUN_TEST(test_header_isRelayed_flag_set);
    RUN_TEST(test_header_isBroadcast_dest_255);
    RUN_TEST(test_header_isBroadcast_dest_not_255);

    // RSSI utilities
    RUN_TEST(test_rssiByteToDbm_zero);
    RUN_TEST(test_rssiDbmToByte_round_trip);

    return UNITY_END();
}
