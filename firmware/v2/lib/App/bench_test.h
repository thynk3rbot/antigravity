/**
 * @file bench_test.h
 * @brief LoRaLink v2 Bench Mode - Diagnostic Hardware Testing
 *
 * Provides compile-time selectable benchmark mode for testing:
 * - HAL components (radio, relays, GPIO)
 * - Transport layer (LoRa TX/RX, encryption)
 * - Packet formatting and deduplication
 * - Mesh coordinator neighbor tracking
 *
 * Enable with: -D BENCH_MODE in platformio.ini build_flags
 */

#pragma once

#ifdef BENCH_MODE

#include <Arduino.h>
#include <cstdint>
#include "../HAL/radio_hal.h"
#include "../HAL/relay_hal.h"
#include "../Transport/lora_transport.h"
#include "../App/control_packet.h"

/**
 * @class BenchTest
 * @brief Hardware diagnostic benchmarks
 */
class BenchTest {
public:
  /**
   * @brief Run complete bench suite
   * @return true if all tests passed
   */
  static bool runAll();

  /**
   * @brief Test radio HAL initialization
   */
  static void testRadioInit();

  /**
   * @brief Test relay GPIO control
   */
  static void testRelayGPIO();

  /**
   * @brief Test LoRa TX/RX loopback
   */
  static void testLoRaLoopback();

  /**
   * @brief Test control packet encoding/decoding
   */
  static void testPacketFormats();

  /**
   * @brief Test packet deduplication
   */
  static void testDeduplication();

  /**
   * @brief Test mesh coordinator
   */
  static void testMeshCoordinator();

private:
  struct BenchResult {
    const char* name;
    bool passed;
    uint32_t durationMs;
    const char* message;
  };

  static void recordResult(const char* testName, bool passed,
                          uint32_t durationMs, const char* message);
  static void printSeparator();
  static void printResults();
};

#endif  // BENCH_MODE
