#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

class ESPNowManager {
public:
  static ESPNowManager &getInstance() {
    static ESPNowManager instance;
    return instance;
  }

  void init();
  void sendToAll(const String &message);
  void sendToPeer(const uint8_t *mac, const String &message);
  bool addPeer(const uint8_t *mac, const char *name);
  bool removePeer(const uint8_t *mac);
  bool poll(String &msgOut);
  void enqueue(const String &msg);

  bool espNowActive;

  // Last send status
  bool lastSendSuccess;
  String lastSentMessage;

  // Receive stats
  int rxCount;
  int txCount;

private:
  ESPNowManager();

  // Receive queue (populated by ISR callback, drained by scheduler)
  static const int QUEUE_SIZE = ESPNOW_QUEUE_SIZE;
  String rxQueue[ESPNOW_QUEUE_SIZE];
  volatile int qHead;
  volatile int qTail;

  // Static callbacks for esp_now
  static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
  static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data,
                         int data_len);
};

#endif // ESPNOW_MANAGER_H
