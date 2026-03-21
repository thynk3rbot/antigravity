/**
 * @file crypto_hal.h
 * @brief AES-128-GCM Encryption HAL for LoRaLink v2
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "mbedtls/gcm.h"
#include <esp_random.h>

#define GCM_IV_SIZE  12
#define GCM_TAG_SIZE 16
#define CRYPTO_OVERHEAD (GCM_IV_SIZE + GCM_TAG_SIZE)

class CryptoHAL {
public:
    static CryptoHAL& getInstance();

    /**
     * @brief Encrypt data using AES-128-GCM
     * @param plain Input plaintext
     * @param plainLen Length of plaintext
     * @param key 16-byte AES key
     * @param out Encrypted output (Must be plainLen + 28 bytes)
     *            Format: | IV[12] | Tag[16] | Ciphertext[N] |
     * @return true on success
     */
    bool encrypt(const uint8_t* plain, size_t plainLen, const uint8_t* key, uint8_t* out);

    /**
     * @brief Decrypt data using AES-128-GCM
     * @param in Encrypted input (| IV[12] | Tag[16] | Ciphertext[N] |)
     * @param inLen Total length of input (N + 28)
     * @param key 16-byte AES key
     * @param out Plaintext output (Must be inLen - 28 bytes)
     * @return true on success (Auth pass)
     */
    bool decrypt(const uint8_t* in, size_t inLen, const uint8_t* key, uint8_t* out);

private:
    CryptoHAL() = default;
};

extern CryptoHAL& cryptoHAL;
