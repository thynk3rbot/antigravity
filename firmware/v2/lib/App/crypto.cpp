/**
 * @file crypto.cpp
 * @brief AES-128-GCM encryption/decryption — mbedTLS implementation
 *
 * Wire format (must match v0.1.0 for fleet interoperability):
 *   [12 bytes: IV/nonce — random per packet]
 *   [N bytes:  ciphertext]
 *   [16 bytes: GCM authentication tag]
 *
 * Total overhead: 28 bytes per packet.
 *
 * AAD for LoRa context: first 3 bytes of ControlPacket header
 *   (type, src, dest) supplied by the caller.
 */

#include "crypto.h"
#include "nvs_config.h"
#include <esp_random.h>

// ============================================================================
// Static member definitions
// ============================================================================

mbedtls_gcm_context Crypto::_ctx;
uint8_t             Crypto::_key[16]       = {0};
bool                Crypto::_initialized   = false;

// ============================================================================
// Public API
// ============================================================================

bool Crypto::begin(const String& hexKey) {
    // Determine which hex key string to use
    String keyStr = hexKey;
    if (keyStr.isEmpty()) {
        keyStr = NVSConfig::getCryptoKey();
    }

    // Fall back to a compile-time default if NVS returns nothing
    if (keyStr.isEmpty()) {
        keyStr = "0102030405060708090A0B0C0D0E0F10";
    }

    // Convert hex string -> 16 raw bytes
    if (!hexToBytes(keyStr, _key, 16)) {
        Serial.println("[Crypto] ERROR: invalid hex key string");
        return false;
    }

    // Initialise (or re-initialise) the GCM context
    if (_initialized) {
        mbedtls_gcm_free(&_ctx);
        _initialized = false;
    }

    mbedtls_gcm_init(&_ctx);

    int ret = mbedtls_gcm_setkey(&_ctx, MBEDTLS_CIPHER_ID_AES, _key, 128);
    if (ret != 0) {
        Serial.printf("[Crypto] ERROR: mbedtls_gcm_setkey failed (%d)\n", ret);
        mbedtls_gcm_free(&_ctx);
        return false;
    }

    _initialized = true;
    Serial.println("[Crypto] AES-128-GCM ready");
    return true;
}

int Crypto::encrypt(
    const uint8_t* plaintext, size_t plaintextLen,
    const uint8_t* aad,       size_t aadLen,
    uint8_t*       output,    size_t outputMaxLen
) {
    if (!_initialized || !plaintext || !output) {
        return -1;
    }

    // Minimum output capacity: IV + ciphertext + tag
    const size_t required = IV_SIZE + plaintextLen + TAG_SIZE;
    if (outputMaxLen < required) {
        Serial.printf("[Crypto] encrypt: output buffer too small (%zu < %zu)\n",
                      outputMaxLen, required);
        return -1;
    }

    // Generate a fresh 12-byte random IV for this packet
    uint8_t iv[IV_SIZE];
    esp_fill_random(iv, IV_SIZE);

    // Layout: [IV][ciphertext][tag]
    uint8_t* ivOut         = output;
    uint8_t* ciphertextOut = output + IV_SIZE;
    uint8_t* tagOut        = output + IV_SIZE + plaintextLen;

    // Copy IV into output
    memcpy(ivOut, iv, IV_SIZE);

    // Encrypt and produce authentication tag in one call
    int ret = mbedtls_gcm_crypt_and_tag(
        &_ctx,
        MBEDTLS_GCM_ENCRYPT,
        plaintextLen,
        iv,   IV_SIZE,
        aad,  aadLen,
        plaintext,
        ciphertextOut,
        TAG_SIZE,
        tagOut
    );

    if (ret != 0) {
        Serial.printf("[Crypto] encrypt: mbedtls_gcm_crypt_and_tag failed (%d)\n", ret);
        return -1;
    }

    return static_cast<int>(required);
}

int Crypto::decrypt(
    const uint8_t* input,  size_t inputLen,
    const uint8_t* aad,    size_t aadLen,
    uint8_t*       output, size_t outputMaxLen
) {
    if (!_initialized || !input || !output) {
        return -1;
    }

    // Input must contain at least IV + tag (no plaintext is valid, but unusual)
    if (inputLen < IV_SIZE + TAG_SIZE) {
        Serial.printf("[Crypto] decrypt: input too short (%zu bytes)\n", inputLen);
        return -1;
    }

    const size_t ciphertextLen = inputLen - IV_SIZE - TAG_SIZE;

    if (outputMaxLen < ciphertextLen) {
        Serial.printf("[Crypto] decrypt: output buffer too small (%zu < %zu)\n",
                      outputMaxLen, ciphertextLen);
        return -1;
    }

    const uint8_t* iv         = input;
    const uint8_t* ciphertext = input + IV_SIZE;
    const uint8_t* tag        = input + IV_SIZE + ciphertextLen;

    // Decrypt and verify authentication tag atomically
    int ret = mbedtls_gcm_auth_decrypt(
        &_ctx,
        ciphertextLen,
        iv,         IV_SIZE,
        aad,        aadLen,
        tag,        TAG_SIZE,
        ciphertext,
        output
    );

    if (ret != 0) {
        // MBEDTLS_ERR_GCM_AUTH_FAILED (-0x0012) means tampered or wrong key
        Serial.printf("[Crypto] decrypt: authentication failed (ret=%d) — wrong key or tampered packet\n", ret);
        return -1;
    }

    return static_cast<int>(ciphertextLen);
}

bool Crypto::hexToBytes(const String& hex, uint8_t* bytes, size_t len) {
    if (!bytes) {
        return false;
    }

    // Each byte requires exactly 2 hex characters
    if (hex.length() != len * 2) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t hi = 0;
        uint8_t lo = 0;

        char ch = hex.charAt(static_cast<unsigned int>(i * 2));
        if      (ch >= '0' && ch <= '9') { hi = static_cast<uint8_t>(ch - '0'); }
        else if (ch >= 'A' && ch <= 'F') { hi = static_cast<uint8_t>(ch - 'A' + 10); }
        else if (ch >= 'a' && ch <= 'f') { hi = static_cast<uint8_t>(ch - 'a' + 10); }
        else { return false; }

        ch = hex.charAt(static_cast<unsigned int>(i * 2 + 1));
        if      (ch >= '0' && ch <= '9') { lo = static_cast<uint8_t>(ch - '0'); }
        else if (ch >= 'A' && ch <= 'F') { lo = static_cast<uint8_t>(ch - 'A' + 10); }
        else if (ch >= 'a' && ch <= 'f') { lo = static_cast<uint8_t>(ch - 'a' + 10); }
        else { return false; }

        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    return true;
}

bool Crypto::isEnabled() {
    return _initialized;
}
