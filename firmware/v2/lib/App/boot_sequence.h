/**
 * @file boot_sequence.h
 * @brief Logic for device startup and component initialization
 */

#pragma once

#include <Arduino.h>

/**
 * @class BootSequence
 * @brief Handles the orchestrated startup of all LoRaLink components
 *
 * This class encapsulates the logic previously held in setup(),
 * providing a modular, staged initialization process:
 * 1. Core (Serial, NVS, Filesystem, Power)
 * 2. HAL (Display, Base Sensors, Radio HAL)
 * 3. Transports (LoRa, BLE, WiFi, ESP-NOW, MQTT)
 * 4. Application (Mesh, Scheduler, Commands)
 * 5. Tasks (FreeRTOS Task Creation)
 */
class BootSequence {
public:
  /**
   * @brief Execute the full device boot sequence
   * Orchestrates initialization across HAL, Transport, and App layers.
   */
  static void run();

private:
  static void initCore();
  static void initHAL();
  static void initTransports();
  static void initApplication();
  static void createTasks();
};
