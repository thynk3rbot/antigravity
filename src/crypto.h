// ============================================================================
//  crypto.h — AES-128 Encryption Wrapper for LoRaLink-AnyToAny
//  (c) 2026 Steven P Williams (spw1.com). All Rights Reserved.
// ============================================================================
#pragma once

#include "mbedtls/aes.h"
#include <Arduino.h>
#include <esp_random.h>
#include <stdint.h>
#include <string.h>

// Default key (Reference only - used if no key saved)
// "LoRaLinkDefault!" = 16 bytes
static const uint8_t DEFAULT_AES_KEY[16] = {'L', 'o', 'R', 'a', 'L', 'i',
                                            'n', 'k', 'D', 'e', 'f', 'a',
                                            'u', 'l', 't', '!'};

// Parse a 32-character hex string into 16 bytes. Returns true on success.
inline bool parseHexKey(const char *hex, uint8_t *out) {
  if (strlen(hex) != 32)
    return false;
  for (int i = 0; i < 16; i++) {
    char hi = hex[i * 2];
    char lo = hex[i * 2 + 1];
    uint8_t hiVal, loVal;
    if (hi >= '0' && hi <= '9')
      hiVal = hi - '0';
    else if (hi >= 'A' && hi <= 'F')
      hiVal = hi - 'A' + 10;
    else if (hi >= 'a' && hi <= 'f')
      hiVal = hi - 'a' + 10;
    else
      return false;
    if (lo >= '0' && lo <= '9')
      loVal = lo - '0';
    else if (lo >= 'A' && lo <= 'F')
      loVal = lo - 'A' + 10;
    else if (lo >= 'a' && lo <= 'f')
      loVal = lo - 'a' + 10;
    else
      return false;
    out[i] = (hiVal << 4) | loVal;
  }
  return true;
}

// Encrypted wire format: | IV[16] | ciphertext[64] | = 80 bytes
#define AES_BLOCK_SIZE 16

// Encrypt a MessagePacket (64 bytes) into an 80-byte output buffer.
inline void encryptPacket(const void *plainPacket, uint8_t *outBuf,
                          const uint8_t *key) {
  uint8_t iv[16];
  esp_fill_random(iv, 16);
  memcpy(outBuf, iv, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);

  uint8_t iv_tmp[16];
  memcpy(iv_tmp, iv, 16);

  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 64, iv_tmp,
                        (const uint8_t *)plainPacket, outBuf + 16);
  mbedtls_aes_free(&aes);
}

// Decrypt an 80-byte buffer into a 64-byte MessagePacket.
inline bool decryptPacket(const uint8_t *inBuf, void *plainPacket,
                          const uint8_t *key) {
  uint8_t iv[16];
  memcpy(iv, inBuf, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);

  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 64, iv, inBuf + 16,
                                  (uint8_t *)plainPacket);
  mbedtls_aes_free(&aes);
  return (ret == 0);
}
