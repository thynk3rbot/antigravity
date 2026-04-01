/**
 * @file crypto_hal.cpp
 * @brief AES-128-GCM Implementation using mbedtls
 */

#include "crypto_hal.h"
#include <cstring>
#include <Arduino.h>

CryptoHAL& cryptoHAL = CryptoHAL::getInstance();

CryptoHAL& CryptoHAL::getInstance() {
    static CryptoHAL instance;
    return instance;
}

bool CryptoHAL::encrypt(const uint8_t* plain, size_t plainLen, const uint8_t* key, uint8_t* out) {
    if (!plain || !key || !out) return false;

    uint8_t iv[GCM_IV_SIZE];
    esp_fill_random(iv, GCM_IV_SIZE);
    memcpy(out, iv, GCM_IV_SIZE);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    ret = mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT, plainLen, iv, GCM_IV_SIZE, 
        nullptr, 0, plain, out + CRYPTO_OVERHEAD, GCM_TAG_SIZE, out + GCM_IV_SIZE);

    mbedtls_gcm_free(&gcm);
    return (ret == 0);
}

bool CryptoHAL::decrypt(const uint8_t* in, size_t inLen, const uint8_t* key, uint8_t* out) {
    if (!in || !key || !out || inLen <= CRYPTO_OVERHEAD) return false;

    size_t plainLen = inLen - CRYPTO_OVERHEAD;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    ret = mbedtls_gcm_auth_decrypt(
        &gcm, plainLen, in, GCM_IV_SIZE, nullptr, 0, in + GCM_IV_SIZE, 
        GCM_TAG_SIZE, in + CRYPTO_OVERHEAD, out);

    mbedtls_gcm_free(&gcm);
    return (ret == 0);
}
