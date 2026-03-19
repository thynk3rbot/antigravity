#pragma once
#include <Arduino.h>
#include <mbedtls/gcm.h>

/**
 * @file crypto.h
 * @brief AES-128-GCM encryption/decryption module
 *
 * Matches the v0.1.0 wire format for fleet interoperability:
 *   [12-byte IV][ciphertext][16-byte GCM tag]
 *
 * Key is loaded from NVSConfig::getCryptoKey() (32-char hex string -> 16 bytes).
 * A fresh random IV is generated per packet via esp_fill_random().
 */

class Crypto {
public:
    /**
     * @brief Initialize with 16-byte AES key from a hex string.
     * @param hexKey  32-char hex string, e.g. "0102030405060708090A0B0C0D0E0F10"
     *                Pass an empty String to load from NVSConfig::getCryptoKey().
     * @return true on success, false if key is invalid or mbedTLS init fails.
     */
    static bool begin(const String& hexKey = "");

    /**
     * @brief Encrypt plaintext with AES-128-GCM.
     *
     * Output layout: [12-byte IV][ciphertext (plaintextLen bytes)][16-byte tag]
     * Total output length = plaintextLen + IV_SIZE + TAG_SIZE.
     *
     * @param plaintext     Input data to encrypt.
     * @param plaintextLen  Number of bytes in plaintext.
     * @param aad           Additional Authenticated Data (not encrypted, but authenticated).
     * @param aadLen        Length of AAD. May be 0.
     * @param output        Caller-supplied output buffer.
     * @param outputMaxLen  Capacity of output buffer (must be >= plaintextLen + 28).
     * @return Encrypted output length on success, -1 on error.
     */
    static int encrypt(
        const uint8_t* plaintext, size_t plaintextLen,
        const uint8_t* aad,       size_t aadLen,
        uint8_t*       output,    size_t outputMaxLen
    );

    /**
     * @brief Decrypt and authenticate an AES-128-GCM packet.
     *
     * Expected input layout: [12-byte IV][ciphertext][16-byte tag]
     * Minimum input length = IV_SIZE + TAG_SIZE = 28 bytes.
     *
     * @param input       Encrypted buffer (IV + ciphertext + tag).
     * @param inputLen    Total length of input buffer.
     * @param aad         Additional Authenticated Data (must match encryption AAD).
     * @param aadLen      Length of AAD. May be 0.
     * @param output      Caller-supplied buffer for decrypted plaintext.
     * @param outputMaxLen Capacity of output buffer.
     * @return Decrypted plaintext length on success, -1 on authentication failure.
     */
    static int decrypt(
        const uint8_t* input,  size_t inputLen,
        const uint8_t* aad,    size_t aadLen,
        uint8_t*       output, size_t outputMaxLen
    );

    /**
     * @brief Convert a hex string to raw bytes.
     * @param hex   Hex string (upper or lower case), e.g. "0102...0F10".
     * @param bytes Output byte array.
     * @param len   Expected number of bytes (hex.length() must == len * 2).
     * @return true on success, false if length is wrong or illegal characters found.
     */
    static bool hexToBytes(const String& hex, uint8_t* bytes, size_t len);

    /**
     * @brief Return true if Crypto::begin() has been called successfully.
     */
    static bool isEnabled();

private:
    static mbedtls_gcm_context _ctx;
    static uint8_t             _key[16];
    static bool                _initialized;

    static constexpr size_t IV_SIZE  = 12;
    static constexpr size_t TAG_SIZE = 16;
};
