#include "msg_manager.h"
#include "nvs_manager.h"
#include "command_manager.h"
#include "../Transport/lora_transport.h"
#include <Arduino.h>
#include <esp_random.h>

// g_ourNodeID is a global set during boot from NVS
extern uint8_t g_ourNodeID;

MsgManager::MsgManager() {
    memset(_dedup,   0, sizeof(_dedup));
    memset(_pending, 0, sizeof(_pending));
}

void MsgManager::init() {
    _nextPacketId = (uint32_t)(esp_random() & 0xFFFF);
    Serial.println("[MSG] MsgManager initialized");

    // Register MSG command: "MSG <dest_hex> <text>"
    // CommandManager::getInstance().registerCommand("MSG",
    //     [](const String& args, TransportType source) {
    //         int sp = args.indexOf(' ');
    //         if (sp < 0) {
    //             Serial.println("[MSG] Usage: MSG <dest_hex> <text>");
    //             return;
    //         }
    //         uint8_t dest = (uint8_t)strtol(args.substring(0, sp).c_str(), nullptr, 16);
    //         String text = args.substring(sp + 1);
    //         MsgManager::getInstance().sendText(dest, text);
    //     });
}

// ── Dedup ─────────────────────────────────────────────────────────────

bool MsgManager::_isDuplicate(uint8_t src, uint32_t packetId) {
    unsigned long now = millis();
    for (int i = 0; i < LMX_DEDUP_SIZE; i++) {
        if (_dedup[i].src == src && _dedup[i].packetId == packetId &&
            (now - _dedup[i].seenMs) < LMX_DEDUP_TTL_MS) {
            return true;
        }
    }
    return false;
}

void MsgManager::_markSeen(uint8_t src, uint32_t packetId) {
    _dedup[_dedupHead] = {src, packetId, millis()};
    _dedupHead = (_dedupHead + 1) % LMX_DEDUP_SIZE;
}

// ── Build & Transmit ──────────────────────────────────────────────────

bool MsgManager::_sendLmxPacket(uint8_t dest, LmxMsgType type, bool wantAck,
                                 const uint8_t* payload, size_t payloadLen) {
    if (payloadLen > LMX_MAX_PAYLOAD) return false;

    uint8_t raw[LMX_MAX_PACKET];
    LmxHeader* hdr = reinterpret_cast<LmxHeader*>(raw);

    hdr->sync[0]   = LMX_SYNC_0;
    hdr->sync[1]   = LMX_SYNC_1;
    hdr->dest      = dest;
    hdr->src       = g_ourNodeID;
    hdr->packetId  = _nextPacketId++;
    hdr->setFlags(3, wantAck, type);  // 3 hop limit
    hdr->hopStart  = 3;
    hdr->_reserved[0] = 0;
    hdr->_reserved[1] = 0;

    if (payloadLen > 0) {
        memcpy(raw + LMX_HEADER_SIZE, payload, payloadLen);
    }

    size_t totalLen = LMX_HEADER_SIZE + payloadLen;

    // Register pending ACK before transmitting
    if (wantAck && dest != LMX_BROADCAST) {
        for (int i = 0; i < MAX_PENDING; i++) {
            if (!_pending[i].active) {
                _pending[i].active        = true;
                _pending[i].dest          = dest;
                _pending[i].packetId      = hdr->packetId;
                _pending[i].packetLen     = totalLen;
                memcpy(_pending[i].packet, raw, totalLen);
                _pending[i].retryCount    = 0;
                _pending[i].lastAttemptMs = millis();
                break;
            }
        }
    }

    // Transmit — LoRaTransport handles AES-128-GCM encryption
    int sent = LoRaTransport::getInstance().send(raw, totalLen);
    if (sent > 0) {
        _txCount++;
        _markSeen(g_ourNodeID, hdr->packetId);
        return true;
    }
    return false;
}

bool MsgManager::sendText(uint8_t dest, const String& text, bool wantAck) {
    Serial.printf("[MSG] TX -> 0x%02X: %s\n", dest, text.c_str());
    return _sendLmxPacket(dest, LmxMsgType::TEXT, wantAck,
                          (const uint8_t*)text.c_str(), text.length());
}

// ── ACK ───────────────────────────────────────────────────────────────

void MsgManager::_sendAck(uint8_t dest, uint32_t originalPacketId, TransportType via) {
    uint8_t payload[4];
    memcpy(payload, &originalPacketId, 4);
    _sendLmxPacket(dest, LmxMsgType::ACK, false, payload, 4);
}

// ── Receive & Relay ───────────────────────────────────────────────────

void MsgManager::handleLmxPacket(const uint8_t* data, size_t len, TransportType source) {
    if (len < LMX_HEADER_SIZE) return;

    const LmxHeader* hdr = reinterpret_cast<const LmxHeader*>(data);
    if (hdr->sync[0] != LMX_SYNC_0 || hdr->sync[1] != LMX_SYNC_1) return;

    // Dedup
    if (_isDuplicate(hdr->src, hdr->packetId)) return;
    _markSeen(hdr->src, hdr->packetId);
    _rxCount++;

    bool forUs = (hdr->dest == g_ourNodeID || hdr->dest == LMX_BROADCAST);

    if (forUs) {
        switch (hdr->msgType()) {
            case LmxMsgType::TEXT: {
                size_t payloadLen = len - LMX_HEADER_SIZE;
                String text = String((const char*)(data + LMX_HEADER_SIZE), payloadLen);
                int hopsUsed = hdr->hopStart - hdr->hopLimit();
                Serial.printf("[MSG] RX <- 0x%02X (%d hops): %s\n",
                              hdr->src, hopsUsed, text.c_str());
                if (_onMessage) _onMessage(hdr->src, text, hopsUsed);
                if (hdr->wantAck()) _sendAck(hdr->src, hdr->packetId, source);
                break;
            }
            case LmxMsgType::ACK: {
                if (len >= LMX_HEADER_SIZE + 4) {
                    uint32_t ackId;
                    memcpy(&ackId, data + LMX_HEADER_SIZE, 4);
                    for (int i = 0; i < MAX_PENDING; i++) {
                        if (_pending[i].active && _pending[i].packetId == ackId) {
                            Serial.printf("[MSG] ACK received for pkt %lu\n", (unsigned long)ackId);
                            _pending[i].active = false;
                            break;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    // Relay if hop limit allows and not unicast to us only
    if (hdr->hopLimit() > 0 && hdr->dest != g_ourNodeID) {
        _rebroadcast(data, len);
    }
}

void MsgManager::_rebroadcast(const uint8_t* raw, size_t len) {
    uint8_t buf[LMX_MAX_PACKET];
    if (len > sizeof(buf)) return;
    memcpy(buf, raw, len);
    LmxHeader* hdr = reinterpret_cast<LmxHeader*>(buf);
    // Decrement hop limit
    uint8_t newHops = hdr->hopLimit() - 1;
    hdr->setFlags(newHops, hdr->wantAck(), hdr->msgType());
    LoRaTransport::getInstance().send(buf, len);
}

// ── Retry Engine ──────────────────────────────────────────────────────

void MsgManager::_checkRetries() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!_pending[i].active) continue;
        if ((now - _pending[i].lastAttemptMs) < LMX_ACK_TIMEOUT_MS) continue;
        if (_pending[i].retryCount >= LMX_MAX_RETRIES) {
            Serial.printf("[MSG] Max retries for pkt %lu, giving up\n",
                          (unsigned long)_pending[i].packetId);
            _pending[i].active = false;
            continue;
        }
        Serial.printf("[MSG] Retry %d for pkt %lu -> 0x%02X\n",
                      _pending[i].retryCount + 1,
                      (unsigned long)_pending[i].packetId,
                      _pending[i].dest);
        LoRaTransport::getInstance().send(_pending[i].packet, _pending[i].packetLen);
        _pending[i].retryCount++;
        _pending[i].lastAttemptMs = now;
    }
}

void MsgManager::tick() {
    _checkRetries();
}
