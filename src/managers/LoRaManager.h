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
