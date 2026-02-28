#include "ESPNowManager.h"
#include "../utils/DebugMacros.h"
#include "DataManager.h"
#include <esp_wifi.h>

// Static instance pointer for callbacks
static ESPNowManager *_espNowInstance = nullptr;

ESPNowManager::ESPNowManager() {
  espNowActive = false;
  lastSendSuccess = false;
  rxCount = 0;
  txCount = 0;
  qHead = 0;
  qTail = 0;
  _espNowInstance = this;
}

void ESPNowManager::onDataSent(const uint8_t *mac_addr,
                               esp_now_send_status_t status) {
  if (_espNowInstance) {
    _espNowInstance->lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    _espNowInstance->txCount++;
    LOG_PRINTF("ESPNOW: TX %s\n",
               status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
  }
}

void ESPNowManager::onDataRecv(const uint8_t *mac_addr, const uint8_t *data,
                               int data_len) {
  if (!_espNowInstance || data_len <= 0 || data_len > 250)
    return;

  // Convert to String
  char buf[251];
  int len = min(data_len, 250);
  memcpy(buf, data, len);
  buf[len] = '\0';

  String msg = String(buf);
  msg.trim();

  if (msg.length() > 0) {
    _espNowInstance->enqueue(msg);
    _espNowInstance->rxCount++;

    // Log the sender MAC
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1],
            mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    LOG_PRINTF("ESPNOW: RX from %s: %s\n", macStr, msg.c_str());
  }
}

void ESPNowManager::init() {
  DataManager &data = DataManager::getInstance();

  if (!data.espNowEnabled) {
    LOG_PRINTLN("ESPNOW: Disabled in config");
    return;
  }

  // WiFi must be in STA mode (or AP+STA) for ESP-NOW
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  }

  // Set channel
  esp_wifi_set_channel(data.espNowChannel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    LOG_PRINTLN("ESPNOW: Init FAILED");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Register saved peers
  for (int i = 0; i < data.numEspNowPeers; i++) {
    if (data.espNowPeers[i].active) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, data.espNowPeers[i].mac, 6);
      peerInfo.channel = data.espNowChannel;
      peerInfo.encrypt = false;

      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        LOG_PRINTF("ESPNOW: Peer %s added (%02X:%02X:%02X:%02X:%02X:%02X)\n",
                   data.espNowPeers[i].name, data.espNowPeers[i].mac[0],
                   data.espNowPeers[i].mac[1], data.espNowPeers[i].mac[2],
                   data.espNowPeers[i].mac[3], data.espNowPeers[i].mac[4],
                   data.espNowPeers[i].mac[5]);
      }
    }
  }

  espNowActive = true;
  LOG_PRINTLN("ESPNOW: Initialized OK");
}

bool ESPNowManager::addPeer(const uint8_t *mac, const char *name) {
  if (!espNowActive)
    return false;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = DataManager::getInstance().espNowChannel;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result == ESP_OK) {
    // Save to DataManager
    DataManager &data = DataManager::getInstance();
    int slot = -1;
    for (int i = 0; i < ESPNOW_MAX_PEERS; i++) {
      if (!data.espNowPeers[i].active) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      data.SaveESPNowPeer(slot, mac, name);
    }
    LOG_PRINTF("ESPNOW: Peer added: %s\n", name);
    return true;
  }
  LOG_PRINTLN("ESPNOW: Failed to add peer");
  return false;
}

bool ESPNowManager::removePeer(const uint8_t *mac) {
  if (!espNowActive)
    return false;
  esp_err_t result = esp_now_del_peer(mac);
  if (result == ESP_OK) {
    DataManager &data = DataManager::getInstance();
    for (int i = 0; i < ESPNOW_MAX_PEERS; i++) {
      if (data.espNowPeers[i].active &&
          memcmp(data.espNowPeers[i].mac, mac, 6) == 0) {
        data.RemoveESPNowPeer(i);
        break;
      }
    }
    return true;
  }
  return false;
}

void ESPNowManager::sendToAll(const String &message) {
  if (!espNowActive)
    return;

  DataManager &data = DataManager::getInstance();
  for (int i = 0; i < data.numEspNowPeers; i++) {
    if (data.espNowPeers[i].active) {
      sendToPeer(data.espNowPeers[i].mac, message);
    }
  }
  lastSentMessage = message;
}

void ESPNowManager::sendToPeer(const uint8_t *mac, const String &message) {
  if (!espNowActive)
    return;

  esp_err_t result =
      esp_now_send(mac, (const uint8_t *)message.c_str(), message.length());
  if (result != ESP_OK) {
    LOG_PRINTLN("ESPNOW: Send error");
  }
}

void ESPNowManager::enqueue(const String &msg) {
  int next = (qTail + 1) % QUEUE_SIZE;
  if (next == qHead)
    return; // queue full
  rxQueue[qTail] = msg;
  qTail = next;
}

bool ESPNowManager::poll(String &msgOut) {
  if (qHead == qTail)
    return false;
  msgOut = rxQueue[qHead];
  qHead = (qHead + 1) % QUEUE_SIZE;
  return true;
}
