#include "msg_manager.h"
#include "nvs_manager.h"
#include "../Transport/lora_transport.h"
#include "../Transport/espnow_transport.h"
#include "../Transport/wifi_transport.h"
#include <Arduino.h>
#include <esp_random.h>
#ifndef UNIT_TEST
#include <WiFi.h>
#endif

extern uint8_t g_ourNodeID;

MsgManager::MsgManager() {
    memset(_dedup,       0, sizeof(_dedup));
    memset(_pending,     0, sizeof(_pending));
    memset(_reassembly,  0, sizeof(_reassembly));
    memset(_neighbors,   0, sizeof(_neighbors));
}

void MsgManager::init() {
    _nextPacketId = (uint32_t)(esp_random() & 0xFFFF);
    Serial.printf("[MSG] MsgManager ready — node 0x%02X\n", g_ourNodeID);
    // MSG command handled by CommandManager::_handleMsg()
    announceNode();
}

// ── Dedup ─────────────────────────────────────────────────────────────

bool MsgManager::_isDuplicate(uint8_t src, uint32_t packetId) {
    unsigned long now = millis();
    for (int i = 0; i < LMX_DEDUP_SIZE; i++) {
        if (_dedup[i].src == src && _dedup[i].packetId == packetId &&
            (now - _dedup[i].seenMs) < LMX_DEDUP_TTL_MS) return true;
    }
    return false;
}

void MsgManager::_markSeen(uint8_t src, uint32_t packetId) {
    _dedup[_dedupHead] = {src, packetId, millis()};
    _dedupHead = (_dedupHead + 1) % LMX_DEDUP_SIZE;
}

// ── Transport Selection ───────────────────────────────────────────────
// Priority (per protocol.json): WiFi HTTP > ESP-NOW > BLE > LoRa
// Checks neighbor capability table from NODE_ANNOUNCE first.
// ESP-NOW is preferred for nearby peers; LoRa is always available fallback.

TransportType MsgManager::_selectTransport(uint8_t dest) {
    if (dest != LMX_BROADCAST) {
        unsigned long now = millis();
        for (int i = 0; i < LMX_NEIGHBOR_SLOTS; i++) {
            if (!_neighbors[i].active || _neighbors[i].src != dest) continue;
            if ((now - _neighbors[i].lastSeenMs) > LMX_NEIGHBOR_TTL_MS) break;
            if ((_neighbors[i].caps & LMX_CAP_ESPNOW) &&
                ESPNowTransport::getInstance().isReady()) {
                return TransportType::ESPNOW;
            }
            break;
        }
    }
    // Broadcast or no neighbor entry: try ESP-NOW, fall back to LoRa
    if (ESPNowTransport::getInstance().isReady()) {
        return TransportType::ESPNOW;
    }
    return TransportType::LORA;
}

// ── Core Transmit ─────────────────────────────────────────────────────

bool MsgManager::_sendLmxPacket(uint8_t dest, LmxMsgType type, bool wantAck,
                                 const uint8_t* payload, size_t payloadLen,
                                 TransportType preferredTransport) {
    if (payloadLen > LMX_MAX_PAYLOAD) return false;

    uint8_t raw[LMX_MAX_PACKET];
    LmxHeader* hdr = reinterpret_cast<LmxHeader*>(raw);

    hdr->sync[0]      = LMX_SYNC_0;
    hdr->sync[1]      = LMX_SYNC_1;
    hdr->dest         = dest;
    hdr->src          = g_ourNodeID;
    hdr->packetId     = _nextPacketId++;
    hdr->setFlags(3, wantAck, type);
    hdr->hopStart     = 3;
    hdr->_reserved[0] = 0;
    hdr->_reserved[1] = 0;

    if (payloadLen > 0) memcpy(raw + LMX_HEADER_SIZE, payload, payloadLen);
    size_t totalLen = LMX_HEADER_SIZE + payloadLen;

    // Register pending ACK before transmitting
    if (wantAck && dest != LMX_BROADCAST) {
        for (int i = 0; i < MAX_PENDING; i++) {
            if (!_pending[i].active) {
                _pending[i] = {true, dest, hdr->packetId, {}, totalLen, 0, millis()};
                memcpy(_pending[i].packet, raw, totalLen);
                break;
            }
        }
    }

    // Route to selected transport
    int sent = -1;
    TransportType transport = (preferredTransport == TransportType::LORA)
                              ? _selectTransport(dest)
                              : preferredTransport;

    if (transport == TransportType::ESPNOW && ESPNowTransport::getInstance().isReady()) {
        sent = ESPNowTransport::getInstance().send(raw, totalLen);
    }
    if (sent <= 0) {
        // Always fall back to LoRa
        sent = LoRaTransport::getInstance().send(raw, totalLen);
    }

    if (sent > 0) {
        _txCount++;
        _markSeen(g_ourNodeID, hdr->packetId);
        return true;
    }
    return false;
}

// ── Fragmentation ─────────────────────────────────────────────────────

bool MsgManager::_sendFragmented(uint8_t dest, const uint8_t* data, size_t len, bool wantAck) {
    uint8_t fragCount = (uint8_t)((len + LMX_FRAG_DATA_SIZE - 1) / LMX_FRAG_DATA_SIZE);
    if (fragCount > LMX_MAX_FRAGMENTS) {
        Serial.printf("[MSG] Message too large (%u bytes, max %u)\n", len, LMX_MAX_MSG_BYTES);
        return false;
    }

    uint32_t origId = _nextPacketId;  // first fragment's packet_id
    uint8_t frag[LMX_FRAG_HDR_SIZE + LMX_FRAG_DATA_SIZE];

    for (uint8_t i = 0; i < fragCount; i++) {
        size_t offset   = i * LMX_FRAG_DATA_SIZE;
        size_t fragData = (offset + LMX_FRAG_DATA_SIZE <= len) ? LMX_FRAG_DATA_SIZE : (len - offset);

        LmxFragHeader* fhdr = reinterpret_cast<LmxFragHeader*>(frag);
        fhdr->fragIndex    = i;
        fhdr->fragCount    = fragCount;
        fhdr->origPacketId = origId;
        memcpy(frag + LMX_FRAG_HDR_SIZE, data + offset, fragData);

        // Only request ACK on last fragment
        bool thisAck = wantAck && (i == fragCount - 1);
        _sendLmxPacket(dest, LmxMsgType::FRAGMENT, thisAck, frag, LMX_FRAG_HDR_SIZE + fragData);
    }
    return true;
}

bool MsgManager::sendText(uint8_t dest, const String& text, bool wantAck) {
    const uint8_t* data = (const uint8_t*)text.c_str();
    size_t len = text.length();

    Serial.printf("[MSG] TX -> 0x%02X (%u bytes)\n", dest, len);

    if (len <= LMX_FRAG_DATA_SIZE) {
        return _sendLmxPacket(dest, LmxMsgType::TEXT, wantAck, data, len);
    }
    return _sendFragmented(dest, data, len, wantAck);
}

// ── ACK ───────────────────────────────────────────────────────────────

void MsgManager::_sendAck(uint8_t dest, uint32_t originalPacketId, TransportType via) {
    uint8_t payload[4];
    memcpy(payload, &originalPacketId, 4);
    _sendLmxPacket(dest, LmxMsgType::ACK, false, payload, 4, via);
}

// ── Reassembly ────────────────────────────────────────────────────────

void MsgManager::_handleFragment(const LmxHeader* hdr, const uint8_t* payload,
                                  size_t payloadLen, TransportType source) {
    if (payloadLen < LMX_FRAG_HDR_SIZE) return;

    const LmxFragHeader* fhdr = reinterpret_cast<const LmxFragHeader*>(payload);
    if (fhdr->fragIndex >= LMX_MAX_FRAGMENTS || fhdr->fragCount > LMX_MAX_FRAGMENTS) return;

    // Find or open a reassembly slot for this origPacketId + src
    LmxReassemblySlot* slot = nullptr;
    for (int i = 0; i < LMX_REASSEMBLY_SLOTS; i++) {
        if (_reassembly[i].active &&
            _reassembly[i].src == hdr->src &&
            _reassembly[i].origPacketId == fhdr->origPacketId) {
            slot = &_reassembly[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < LMX_REASSEMBLY_SLOTS; i++) {
            if (!_reassembly[i].active) {
                slot = &_reassembly[i];
                memset(slot, 0, sizeof(*slot));
                slot->active       = true;
                slot->src          = hdr->src;
                slot->origPacketId = fhdr->origPacketId;
                slot->fragCount    = fhdr->fragCount;
                break;
            }
        }
    }
    if (!slot) {
        Serial.println("[MSG] Reassembly: no free slot");
        return;
    }

    uint8_t idx = fhdr->fragIndex;
    if (!(slot->received & (1 << idx))) {
        size_t dataLen = payloadLen - LMX_FRAG_HDR_SIZE;
        if (dataLen > LMX_FRAG_DATA_SIZE) dataLen = LMX_FRAG_DATA_SIZE;
        memcpy(slot->data[idx], payload + LMX_FRAG_HDR_SIZE, dataLen);
        slot->dataLen[idx] = (uint8_t)dataLen;
        slot->received |= (1 << idx);
        slot->lastFragMs = millis();
    }

    // Check if all fragments received
    uint8_t fullMask = (1 << slot->fragCount) - 1;
    if ((slot->received & fullMask) == fullMask) {
        // Reassemble
        String text;
        for (uint8_t i = 0; i < slot->fragCount; i++) {
            text += String((char*)slot->data[i]).substring(0, slot->dataLen[i]);
        }
        int hopsUsed = hdr->hopStart - hdr->hopLimit();
        Serial.printf("[MSG] Reassembled %u frags from 0x%02X: %s\n",
                      slot->fragCount, hdr->src, text.c_str());
        if (_onMessage) _onMessage(hdr->src, text, hopsUsed);
        if (hdr->wantAck()) _sendAck(hdr->src, fhdr->origPacketId, source);
        slot->active = false;
    }
}

void MsgManager::_checkReassemblyTimeouts() {
    unsigned long now = millis();
    for (int i = 0; i < LMX_REASSEMBLY_SLOTS; i++) {
        if (_reassembly[i].active &&
            (now - _reassembly[i].lastFragMs) > LMX_REASSEMBLY_TIMEOUT) {
            Serial.printf("[MSG] Reassembly timeout for origId %lu from 0x%02X\n",
                          (unsigned long)_reassembly[i].origPacketId, _reassembly[i].src);
            _reassembly[i].active = false;
        }
    }
}

// ── Receive & Relay ───────────────────────────────────────────────────

void MsgManager::handleLmxPacket(const uint8_t* data, size_t len, TransportType source) {
    if (len < LMX_HEADER_SIZE) return;
    const LmxHeader* hdr = reinterpret_cast<const LmxHeader*>(data);
    if (hdr->sync[0] != LMX_SYNC_0 || hdr->sync[1] != LMX_SYNC_1) return;

    if (_isDuplicate(hdr->src, hdr->packetId)) return;
    _markSeen(hdr->src, hdr->packetId);
    _rxCount++;

    // Emit [LMX] hex line for PC daemon to parse
    Serial.print("[LMX] ");
    for (size_t i = 0; i < len; i++) Serial.printf("%02X", data[i]);
    Serial.println();

    bool forUs = (hdr->dest == g_ourNodeID || hdr->dest == LMX_BROADCAST);
    const uint8_t* payload = data + LMX_HEADER_SIZE;
    size_t payloadLen = len - LMX_HEADER_SIZE;

    if (forUs) {
        switch (hdr->msgType()) {
            case LmxMsgType::TEXT: {
                String text = String((const char*)payload, payloadLen);
                int hopsUsed = hdr->hopStart - hdr->hopLimit();
                Serial.printf("[MSG] RX <- 0x%02X (%d hops): %s\n",
                              hdr->src, hopsUsed, text.c_str());
                if (_onMessage) _onMessage(hdr->src, text, hopsUsed);
                if (hdr->wantAck()) _sendAck(hdr->src, hdr->packetId, source);
                break;
            }
            case LmxMsgType::FRAGMENT:
                _handleFragment(hdr, payload, payloadLen, source);
                break;
            case LmxMsgType::ACK: {
                if (payloadLen >= 4) {
                    uint32_t ackId;
                    memcpy(&ackId, payload, 4);
                    for (int i = 0; i < MAX_PENDING; i++) {
                        if (_pending[i].active && _pending[i].packetId == ackId) {
                            Serial.printf("[MSG] ACK pkt %lu\n", (unsigned long)ackId);
                            _pending[i].active = false;
                            break;
                        }
                    }
                }
                break;
            }
            case LmxMsgType::NODE_ANNOUNCE: {
                // Payload: [caps:1][wifiIP:4][name:8]
                if (payloadLen >= 5) {
                    uint8_t  caps   = payload[0];
                    uint32_t wifiIP = 0;
                    memcpy(&wifiIP, payload + 1, 4);
                    char name[9] = {};
                    if (payloadLen >= 13) memcpy(name, payload + 5, 8);
                    _updateNeighbor(hdr->src, caps, wifiIP, name);
                }
                break;
            }
            default:
                break;
        }
    }

    // Relay if hops remain and not unicast-to-us-only
    if (hdr->hopLimit() > 0 && hdr->dest != g_ourNodeID) {
        _rebroadcast(data, len);
    }
}

void MsgManager::_rebroadcast(const uint8_t* raw, size_t len) {
    uint8_t buf[LMX_MAX_PACKET];
    if (len > sizeof(buf)) return;
    memcpy(buf, raw, len);
    LmxHeader* hdr = reinterpret_cast<LmxHeader*>(buf);
    hdr->setFlags(hdr->hopLimit() - 1, hdr->wantAck(), hdr->msgType());
    LoRaTransport::getInstance().send(buf, len);
}

// ── Retry Engine ──────────────────────────────────────────────────────

void MsgManager::_checkRetries() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) continue;
        if ((now - _pending[i].lastAttemptMs) < LMX_ACK_TIMEOUT_MS) continue;
        if (_pending[i].retryCount >= LMX_MAX_RETRIES) {
            Serial.printf("[MSG] Max retries for pkt %lu\n", (unsigned long)_pending[i].packetId);
            _pending[i].active = false;
            continue;
        }
        Serial.printf("[MSG] Retry %d pkt %lu -> 0x%02X\n",
                      _pending[i].retryCount + 1,
                      (unsigned long)_pending[i].packetId, _pending[i].dest);
        LoRaTransport::getInstance().send(_pending[i].packet, _pending[i].packetLen);
        _pending[i].retryCount++;
        _pending[i].lastAttemptMs = now;
    }
}

// ── Neighbor Table ────────────────────────────────────────────────────

void MsgManager::_updateNeighbor(uint8_t src, uint8_t caps, uint32_t wifiIP, const char* name) {
    unsigned long now = millis();
    for (int i = 0; i < LMX_NEIGHBOR_SLOTS; i++) {
        if (_neighbors[i].active && _neighbors[i].src == src) {
            _neighbors[i].caps       = caps;
            _neighbors[i].wifiIP     = wifiIP;
            _neighbors[i].lastSeenMs = now;
            if (name && name[0]) strncpy(_neighbors[i].name, name, 8);
            Serial.printf("[MSG] Neighbor updated: 0x%02X caps=0x%02X\n", src, caps);
            return;
        }
    }
    for (int i = 0; i < LMX_NEIGHBOR_SLOTS; i++) {
        if (!_neighbors[i].active) {
            _neighbors[i].active     = true;
            _neighbors[i].src        = src;
            _neighbors[i].caps       = caps;
            _neighbors[i].wifiIP     = wifiIP;
            _neighbors[i].lastSeenMs = now;
            memset(_neighbors[i].name, 0, 9);
            if (name && name[0]) strncpy(_neighbors[i].name, name, 8);
            Serial.printf("[MSG] Neighbor added: 0x%02X caps=0x%02X\n", src, caps);
            return;
        }
    }
    Serial.println("[MSG] Neighbor table full");
}

void MsgManager::announceNode() {
    uint8_t caps = LMX_CAP_LORA;
    if (ESPNowTransport::getInstance().isReady()) caps |= LMX_CAP_ESPNOW;
#ifndef UNIT_TEST
    uint32_t wifiIP = 0;
    if (WiFi.status() == WL_CONNECTED) {
        caps |= LMX_CAP_WIFI;
        wifiIP = (uint32_t)WiFi.localIP();
    }
#else
    uint32_t wifiIP = 0;
#endif
    uint8_t payload[13] = {};
    payload[0] = caps;
    memcpy(payload + 1, &wifiIP, 4);
    String nodeName = NVSManager::getNodeID("Node");
    strncpy((char*)(payload + 5), nodeName.c_str(), 8);
    _sendLmxPacket(LMX_BROADCAST, LmxMsgType::NODE_ANNOUNCE, false,
                   payload, sizeof(payload), TransportType::LORA);
    _lastAnnounceMs = millis();
    Serial.printf("[MSG] NODE_ANNOUNCE caps=0x%02X\n", caps);
}

// ── Tick ──────────────────────────────────────────────────────────────

void MsgManager::tick() {
    _checkRetries();
    _checkReassemblyTimeouts();
    unsigned long now = millis();
    if ((now - _lastAnnounceMs) >= 60000UL) {
        announceNode();
    }
}
