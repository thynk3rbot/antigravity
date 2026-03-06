#include "LoRaManager.h"
#include "../utils/DebugMacros.h"
#include "MQTTManager.h"
#include "PerformanceManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>

// ISR Trampoline
LoRaManager *_loraInstance = NULL;
void IRAM_ATTR setFlag(void) {
  if (_loraInstance) {
    _loraInstance->receivedFlag = true;
  }
}

// Global callback pointer (updated to use CommInterface)
void (*_msgCallback)(const String &, CommInterface) = NULL;

LoRaManager::LoRaManager() {
  loraActive = false;
  receivedFlag = false;
  lastRssi = 0;
  lastSnr = 0;
  hashIndex = 0;
  _loraInstance = this;
  for (int i = 0; i < HASH_BUFFER_SIZE; i++)
    seenMsgHashes[i] = 0;
  for (int i = 0; i < MAX_PENDING_ACKS; i++) {
    ackQueue[i].active = false;
  }
  memcpy(currentKey, DEFAULT_AES_KEY, 16);
}

void LoRaManager::Init() {
  LOG_PRINTLN("LoRa: Init Start");
  Serial.flush();

  SPI.begin(9, 11, 10, PIN_LORA_CS);
  LOG_PRINTLN("LoRa: SPI Started");
  Serial.flush();

  Module *mod =
      new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
  radio = new SX1262(mod);

  DataManager &data = DataManager::getInstance();
  if (data.GetCryptoKey(currentKey)) {
    LOG_PRINTLN("LoRa: Loaded custom AES-GCM key");
  } else {
    LOG_PRINTLN("LoRa: Using Default AES-GCM key");
    memcpy(currentKey, DEFAULT_AES_KEY, 16);
  }
  Serial.flush();

  int state = radio->begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC,
                           LORA_PWR, 8);
  if (state == RADIOLIB_ERR_NONE) {
    loraActive = true;
    radio->setPacketReceivedAction(setFlag);
    radio->startReceive();
    LOG_PRINTLN("LoRa: Initialized OK");
  } else {
    LOG_PRINT("LoRa: Init Failed, code ");
    LOG_PRINTLN(state);
  }
  Serial.flush();
}

void LoRaManager::SetKey(const uint8_t *newKey) {
  memcpy(currentKey, newKey, 16);
  LOG_PRINTLN("LoRa: Key Updated");
}

void LoRaManager::SetCallback(void (*cb)(const String &, CommInterface)) {
  _msgCallback = cb;
}

uint32_t LoRaManager::getMsgHash(MessagePacket *p) {
  uint32_t hash = 5381;
  for (int i = 0; i < 16; i++)
    hash = ((hash << 5) + hash) + p->sender[i];
  for (int i = 0; i < 45; i++)
    hash = ((hash << 5) + hash) + p->text[i];
  return hash;
}

uint16_t LoRaManager::calculateChecksum(MessagePacket *p) {
  uint16_t sum = 0;
  uint8_t *data = (uint8_t *)p;
  for (size_t i = 0; i < sizeof(MessagePacket) - 2; i++) {
    sum += data[i];
  }
  return sum;
}

bool LoRaManager::hasSeenMessage(uint32_t hash) {
  for (int i = 0; i < HASH_BUFFER_SIZE; i++) {
    if (seenMsgHashes[i] == hash)
      return true;
  }
  return false;
}

void LoRaManager::markMessageSeen(uint32_t hash) {
  seenMsgHashes[hashIndex] = hash;
  hashIndex = (hashIndex + 1) % HASH_BUFFER_SIZE;
}

void LoRaManager::SendLoRa(const String &text) {
  if (!loraActive)
    return;

  DataManager &data = DataManager::getInstance();
  DisplayManager &display = DisplayManager::getInstance();

  memset(&txPacket, 0, sizeof(MessagePacket)); // Explicitly wipe entire packet
  strncpy(txPacket.sender, data.myId.c_str(), sizeof(txPacket.sender) - 1);
  strncpy(txPacket.text, text.c_str(), sizeof(txPacket.text) - 1);
  txPacket.ttl = MAX_TTL;

  lastMsgSent = text;

  data.LogMessage(data.myId, 0, text);
  display.SetDisplayActive(true);
  display.DrawUi();

  txPacket.checksum = calculateChecksum(&txPacket);
  encryptPacket(&txPacket, encBuf, currentKey);
  radio->transmit(encBuf, ENCRYPTED_PACKET_SIZE);

  // Track Time on Air (approximate based on byte count, SF, BW etc. or using
  // radiolib's built in getTimeOnAir)
  unsigned long toa = radio->getTimeOnAir(ENCRYPTED_PACKET_SIZE) / 1000;
  PerformanceManager::getInstance().addTimeOnAir(toa);

  receivedFlag = false;
  radio->setPacketReceivedAction(setFlag);
  radio->startReceive();
}

void LoRaManager::HandleRx() {
  static int rxPollCount = 0;
  rxPollCount++;

  // Periodic diagnostic: every 120 polls (~60s at 500ms)
  if (rxPollCount % 120 == 0) {
    LOG_PRINTF("LORA-DIAG: poll=%d, flag=%d, active=%d, DIO1=%d\n", rxPollCount,
               receivedFlag ? 1 : 0, loraActive ? 1 : 0,
               digitalRead(PIN_LORA_DIO1));
    // Force re-arm the receiver
    radio->setPacketReceivedAction(setFlag);
    radio->startReceive();
  }

  if (!loraActive || !receivedFlag)
    return;
  receivedFlag = false;
  LOG_PRINTLN("LORA: *** RX EVENT ***");

  uint8_t rxEncBuf[ENCRYPTED_PACKET_SIZE];
  int state = radio->readData(rxEncBuf, ENCRYPTED_PACKET_SIZE);

  if (state != RADIOLIB_ERR_NONE) {
    radio->startReceive();
    return;
  }

  lastRssi = radio->getRSSI();
  lastSnr = radio->getSNR();

  ProcessPacket(rxEncBuf, ENCRYPTED_PACKET_SIZE);
}

void LoRaManager::ProcessPacket(uint8_t *rxEncBuf, int size) {
  if (!decryptPacket(rxEncBuf, &rxPacket, currentKey)) {
    LOG_PRINTLN("CRYPTO: GCM auth failed (Wrong Key or Tampered)");
    radio->startReceive();
    return;
  }

  rxPacket.sender[15] = '\0';
  rxPacket.text[44] =
      '\0'; // Strictly enforce null-termination within the 45-byte block

  if (rxPacket.checksum != calculateChecksum(&rxPacket)) {
    LOG_PRINTLN("LORA: Integrity Fail (Noise/Garbage)");
    radio->startReceive();
    return;
  }

  // Validate ASCII
  for (int i = 0; i < 15 && rxPacket.sender[i] != '\0'; i++) {
    if (rxPacket.sender[i] < 0x20 || rxPacket.sender[i] > 0x7E) {
      LOG_PRINTLN("CRYPTO: Bad packet (Wrong Key?)");
      radio->startReceive();
      return;
    }
  }

  String sender = String(rxPacket.sender);
  String text = String(rxPacket.text);

  uint32_t msgHash = getMsgHash(&rxPacket);
  if (hasSeenMessage(msgHash)) {
    radio->startReceive();
    return;
  }
  markMessageSeen(msgHash);

  DataManager &data = DataManager::getInstance();

  uint8_t hops = 0;
  if (MAX_TTL >= rxPacket.ttl) {
    hops = MAX_TTL - rxPacket.ttl;
  }
  data.SawNode(rxPacket.sender, lastRssi, hops);

  if (text.startsWith("{") && text.indexOf("\"t\":\"T\"") > 0) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, text);
    if (!error) {
      uint32_t uptime = doc["u"] | 0;
      float bat = doc["b"] | 0.0f;
      uint8_t rst = doc["r"] | 0;
      uint8_t hops = doc["h"] | 0;
      data.UpdateNode(rxPacket.sender, uptime, bat, rst, 0.0f, 0.0f, lastRssi,
                      hops);
      LOG_PRINTF("TEL: %s (hops=%d)\n", sender.c_str(), hops);

      if (data.streamToSerial) {
        Serial.printf("DATA,%s,%.2f,%d,%d\n", sender.c_str(), bat, lastRssi,
                      hops);
      }
      MQTTManager::getInstance().publishTelemetry(sender, bat, lastRssi, hops);

      lastMsgReceived = "TEL [" + sender + "]";
      radio->startReceive();
      return;
    }
  }

  // Telemetry or Binary Check
  bool hasNonPrintable = false;
  for (int i = 0; i < 45 && rxPacket.text[i] != '\0'; i++) {
    if (rxPacket.text[i] < 0x20 || rxPacket.text[i] > 0x7E) {
      hasNonPrintable = true;
      break;
    }
  }

  if (hasNonPrintable) {
    LOG_PRINTLN("LORA: RX Garbled (Key Mismatch or Noise)");
    text = "<GARBLED>";
  }

  lastMsgReceived = "[" + sender + "] " + text;
  data.LogMessage(sender, radio->getRSSI(), text);

  if (data.streamToSerial) {
    Serial.printf("MSG,%s,%d,%s\n", sender.c_str(), radio->getRSSI(),
                  text.c_str());
  }
  MQTTManager::getInstance().publishMessage(sender, radio->getRSSI(), text);

  bool clean = true;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] < 0x20 || text[i] > 0x7E) {
      clean = false;
      break;
    }
  }
  if (clean) {
    LOG_PRINTLN("RX LORA: [" + sender + "] " + text);
  } else {
    LOG_PRINTLN("RX LORA: [" + sender + "] <Binary/Garbage>");
  }

  // Callback to CommandManager with CommInterface::COMM_LORA
  if (_msgCallback) {
    _msgCallback(text, CommInterface::COMM_LORA);
  }

  // Clear Pending ACK if this is an ACK response
  if (text.startsWith("ACK: ")) {
    clearPendingAck(sender);
  }

  // Repeater Logic
  if (data.repeaterEnabled && !text.startsWith("ACK:")) {
    String target = "";
    int space = text.indexOf(' ');
    if (space > 0)
      target = text.substring(0, space);
    else
      target = text;

    bool isForMeTarget = target.equalsIgnoreCase(data.myId);
    bool isForAll = target.equalsIgnoreCase("ALL");

    if (!isForMeTarget || isForAll) {
      if (rxPacket.ttl > 0) {
        int jitter = random(150, 500);
        LOG_PRINTF("RPTR: Propagation delay %d ms...\n", jitter);
        delay(jitter);
        rxPacket.ttl--;
        rxPacket.checksum = calculateChecksum(&rxPacket);
        uint8_t fwdEncBuf[ENCRYPTED_PACKET_SIZE];
        encryptPacket(&rxPacket, fwdEncBuf, currentKey);
        radio->transmit(fwdEncBuf, ENCRYPTED_PACKET_SIZE);
        data.LogMessage("RPTR", radio->getRSSI(), "Propagation: " + text);
      } else {
        LOG_PRINTLN("RPTR: Packet dropped (TTL=0)");
      }
    }
  }

  DisplayManager::getInstance().DrawUi();
  radio->startReceive();
}

void LoRaManager::DumpDiagnostics() {
  if (!loraActive) {
    LOG_PRINTLN("LoRa: Radio not active");
    return;
  }
  LOG_PRINTF("LoRa Diag: RSSI=%d, SNR=%.1f\n", lastRssi, lastSnr);
  LOG_PRINTF("LoRa Config: Freq=%.1f, BW=%.1f, SF=%d, SYNC=0x%02X\n", LORA_FREQ,
             LORA_BW, LORA_SF, LORA_SYNC);
}

void LoRaManager::SendHeartbeat() {
  if (!loraActive)
    return;

  DataManager &data = DataManager::getInstance();

  memset(&txPacket, 0, sizeof(txPacket));
  strncpy(txPacket.sender, data.myId.c_str(), sizeof(txPacket.sender) - 1);

  JsonDocument doc;
  doc["t"] = "T";
  doc["u"] = millis() / 1000;
  doc["h"] = 0; // hop count: originated here

  PerformanceManager &perf = PerformanceManager::getInstance();

  float batVal = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
  String rstReason = String(perf.getResetReason());

  // Dirty-flag suppression: skip TX if state hasn't changed meaningfully
  bool batChanged = fabs(batVal - _lastHBBat) > 0.05f;
  bool rstChanged = (rstReason != _lastHBRst);
  bool forceTX    = (_hbSkipCount >= HB_FORCE_INTERVAL);

  if (!batChanged && !rstChanged && !forceTX) {
    _hbSkipCount++;
    LOG_PRINTF("LORA: HB suppressed (skip %d/%d, bat=%.2f)\n",
               _hbSkipCount, HB_FORCE_INTERVAL, batVal);
    return;
  }

  // State changed or forced — update references and transmit
  _lastHBBat   = batVal;
  _lastHBRst   = rstReason;
  _hbSkipCount = 0;

  doc["b"] = round(batVal * 100.0) / 100.0;
  doc["r"] = 0;
  doc["l_avg"] = perf.getLoopAvgMs();
  doc["l_max"] = perf.getLoopMaxMs();
  doc["toa"] = perf.getTimeOnAir();
  doc["rst"] = perf.getResetReason();
  if (perf.isConfiguratorAttached()) {
    doc["cfg"] = 1;
  }

  String json;
  serializeJson(doc, json);
  strncpy(txPacket.text, json.c_str(), sizeof(txPacket.text) - 1);

  txPacket.ttl = MAX_TTL;
  txPacket.checksum = calculateChecksum(&txPacket);
  encryptPacket(&txPacket, encBuf, currentKey);

  int txState = radio->transmit(encBuf, ENCRYPTED_PACKET_SIZE);
  LOG_PRINTF("LORA: TX result=%d, size=%d\n", txState, ENCRYPTED_PACKET_SIZE);
  receivedFlag = false;
  radio->setPacketReceivedAction(setFlag);
  int rxState = radio->startReceive();
  LOG_PRINTF("LORA: RX re-armed, state=%d\n", rxState);

  LOG_PRINTLN("LORA: Heartbeat Sent -> " + json);
}
void LoRaManager::QueueReliableCommand(const String &targetId,
                                       const String &commandText) {
  for (int i = 0; i < MAX_PENDING_ACKS; i++) {
    if (!ackQueue[i].active) {
      ackQueue[i].targetId = targetId;
      ackQueue[i].commandText = commandText;
      ackQueue[i].retryCount = 0;
      ackQueue[i].lastAttemptMs = millis();
      ackQueue[i].active = true;
      LOG_PRINTLN("LORA: Queued reliable transmission -> " + targetId);
      SendLoRa(targetId + " " + commandText);
      return;
    }
  }
  LOG_PRINTLN("ERR: ACK Queue is full!");
}

void LoRaManager::clearPendingAck(const String &targetId) {
  for (int i = 0; i < MAX_PENDING_ACKS; i++) {
    if (ackQueue[i].active && ackQueue[i].targetId.equalsIgnoreCase(targetId)) {
      ackQueue[i].active = false;
      LOG_PRINTLN("LORA: RX ACK Verified from " + targetId);
      return;
    }
  }
}

void LoRaManager::periodicTick() {
  if (!loraActive)
    return;

  unsigned long now = millis();
  for (int i = 0; i < MAX_PENDING_ACKS; i++) {
    if (ackQueue[i].active) {
      if (now - ackQueue[i].lastAttemptMs > 3000) {
        ackQueue[i].retryCount++;
        if (ackQueue[i].retryCount > 3) {
          ackQueue[i].active = false;
          LOG_PRINTLN("LORA: Delivery Failed (Max retries) -> " +
                      ackQueue[i].targetId);
          DataManager::getInstance().LogMessage(
              "SYS", 0, "Delivery failed -> " + ackQueue[i].targetId);
        } else {
          ackQueue[i].lastAttemptMs = now;
          LOG_PRINTF("LORA: Resending reliable command (Try %d/3)...\n",
                     ackQueue[i].retryCount);
          SendLoRa(ackQueue[i].targetId + " " + ackQueue[i].commandText);
        }
      }
    }
  }
}
