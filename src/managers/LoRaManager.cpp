#include "LoRaManager.h"
#include "../utils/DebugMacros.h"
#include <Arduino.h>
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
  if (data.GetAesKey(currentKey)) {
    LOG_PRINTLN("LoRa: Loaded saved AES Key");
  } else {
    LOG_PRINTLN("LoRa: Using Default AES Key");
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
  for (int i = 0; i < 46; i++)
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

  memset(&txPacket, 0, sizeof(txPacket));
  strncpy(txPacket.sender, data.myId.c_str(), sizeof(txPacket.sender) - 1);
  strncpy(txPacket.text, text.c_str(), sizeof(txPacket.text) - 1);

  lastMsgSent = text;
  data.LogMessage("TX> " + text);
  display.SetDisplayActive(true);
  display.DrawUi();

  txPacket.checksum = calculateChecksum(&txPacket);
  encryptPacket(&txPacket, encBuf, currentKey);
  radio->transmit(encBuf, ENCRYPTED_PACKET_SIZE);
  receivedFlag = false;
  radio->startReceive();
}

void LoRaManager::HandleRx() {
  if (!loraActive || !receivedFlag)
    return;
  receivedFlag = false;

  uint8_t rxEncBuf[ENCRYPTED_PACKET_SIZE];
  int state = radio->readData(rxEncBuf, ENCRYPTED_PACKET_SIZE);

  if (state != RADIOLIB_ERR_NONE) {
    radio->startReceive();
    return;
  }

  lastRssi = radio->getRSSI();
  lastSnr = radio->getSNR();

  decryptPacket(rxEncBuf, &rxPacket, currentKey);
  ProcessPacket(rxEncBuf, ENCRYPTED_PACKET_SIZE);
}

void LoRaManager::ProcessPacket(uint8_t *rxEncBuf, int size) {
  decryptPacket(rxEncBuf, &rxPacket, currentKey);

  rxPacket.sender[15] = '\0';
  rxPacket.text[45] = '\0';

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

  // Telemetry or Binary Check
  bool hasNonPrintable = false;
  for (int i = 0; i < 46 && rxPacket.text[i] != '\0'; i++) {
    if (rxPacket.text[i] < 0x20 || rxPacket.text[i] > 0x7E) {
      hasNonPrintable = true;
      break;
    }
  }

  if (hasNonPrintable) {
    TelemetryPacket *tp = (TelemetryPacket *)rxPacket.text;
    if (tp->uptime > 0 && tp->uptime < 10000000 && tp->battery > 2.0 &&
        tp->battery < 5.0) {
      data.UpdateNode(rxPacket.sender, tp, lastRssi);
      LOG_PRINTF("TEL: %s\n", sender.c_str());
      lastMsgReceived = "TEL [" + sender + "]";
      radio->startReceive();
      return;
    } else {
      LOG_PRINTLN("LORA: RX Garbled (Key Mismatch or Noise)");
      text = "<GARBLED>";
    }
  }

  lastMsgReceived = "[" + sender + "] " + text;
  data.LogMessage("RX [" + sender + "] " + text);

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

  // Callback to CommandManager with CommInterface::LORA
  if (_msgCallback) {
    _msgCallback(text, CommInterface::LORA);
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
      int jitter = random(200, 1000);
      LOG_PRINTF("RPTR: Propagation delay %d ms...\n", jitter);
      delay(jitter);
      radio->transmit(rxEncBuf, ENCRYPTED_PACKET_SIZE);
      data.LogMessage("RPTR> Propagation: " + text);
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

  TelemetryPacket tp;
  tp.uptime = millis() / 1000;
  tp.battery = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
  tp.rssi = lastRssi;
  tp.resetCode = 0;
  tp.lat = 0.0;
  tp.lon = 0.0;

  memcpy(txPacket.text, &tp, sizeof(tp));
  txPacket.checksum = calculateChecksum(&txPacket);
  encryptPacket(&txPacket, encBuf, currentKey);

  radio->transmit(encBuf, ENCRYPTED_PACKET_SIZE);
  receivedFlag = false;
  radio->startReceive();

  LOG_PRINTLN("LORA: Heartbeat Sent");
}
