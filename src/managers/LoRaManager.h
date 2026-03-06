#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include "../config.h"
#include "../crypto.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <cstring>

void IRAM_ATTR setFlag(void);

struct PendingAck {
  String targetId;
  String commandText;
  int retryCount;
  unsigned long lastAttemptMs;
  bool active;
};

class LoRaManager {
public:
  static void SetCallback(void (*cb)(const String &, CommInterface));
  static LoRaManager &getInstance() {
    static LoRaManager instance;
    return instance;
  }

  void Init();
  void SendLoRa(const String &text);
  void SendHeartbeat();
  void HandleRx();
  void ProcessPacket(uint8_t *rxEncBuf, int size);
  void SetKey(const uint8_t *newKey);
  void DumpDiagnostics();
  void QueueReliableCommand(const String &targetId, const String &commandText);
  void clearPendingAck(const String &targetId);
  void periodicTick();

  bool loraActive;
  volatile bool receivedFlag;

  int lastRssi;
  float lastSnr;
  String lastMsgReceived;
  String lastMsgSent;

  uint8_t currentKey[16];

private:
  LoRaManager();
  SX1262 *radio;

  static const int MAX_PENDING_ACKS = 5;
  PendingAck ackQueue[MAX_PENDING_ACKS];

  // Dirty-flag heartbeat suppression — skip TX when state hasn't changed
  float _lastHBBat = -1.0f;   // Last transmitted battery voltage
  String _lastHBRst;           // Last transmitted reset reason
  uint8_t _hbSkipCount = 0;    // Consecutive skips since last forced TX
  static const uint8_t HB_FORCE_INTERVAL = 12; // Force TX every N skips (~60 min at 300s)

  uint32_t seenMsgHashes[HASH_BUFFER_SIZE];
  int hashIndex;
  uint8_t encBuf[ENCRYPTED_PACKET_SIZE];
  MessagePacket txPacket;
  MessagePacket rxPacket;

  uint32_t getMsgHash(MessagePacket *p);
  uint16_t calculateChecksum(MessagePacket *p);
  bool hasSeenMessage(uint32_t hash);
  void markMessageSeen(uint32_t hash);
};

#endif // LORA_MANAGER_H
