/**
 * @file test_nvs_manager.cpp
 * @brief Unit tests for NVSManager persistence module
 *
 * Tests run natively (no hardware) using mock Preferences. Covers:
 * - Device ID storage and validation (1-254)
 * - WiFi SSID/password storage with length limits
 * - AES-128 encryption key (16-byte) storage
 * - Factory reset (clear all)
 * - Configuration state checking
 */

#include <unity.h>
#include "../../lib/Storage/nvs_manager.h"

// ---------------------------------------------------------------------------
// setUp / tearDown (required by Unity/PlatformIO test runner)
// ---------------------------------------------------------------------------

void setUp() {
    // Clear all config before each test
    NVSManager::init();
    NVSManager::clear();
}

void tearDown() {
    // Clean up after each test
    NVSManager::clear();
}

// ---------------------------------------------------------------------------
// Tests: Initialization
// ---------------------------------------------------------------------------

void test_init_returns_true() {
    // init() should always succeed on first call (or when already initialized)
    TEST_ASSERT_TRUE(NVSManager::init());
}

void test_init_idempotent() {
    // Multiple calls to init() should all succeed
    TEST_ASSERT_TRUE(NVSManager::init());
    TEST_ASSERT_TRUE(NVSManager::init());
    TEST_ASSERT_TRUE(NVSManager::init());
}

// ---------------------------------------------------------------------------
// Tests: Device ID
// ---------------------------------------------------------------------------

void test_getDeviceID_returns_0_if_not_set() {
    NVSManager::clear();
    TEST_ASSERT_EQUAL_UINT8(0, NVSManager::getDeviceID());
}

void test_setDeviceID_stores_value() {
    NVSManager::setDeviceID(42);
    TEST_ASSERT_EQUAL_UINT8(42, NVSManager::getDeviceID());
}

void test_setDeviceID_rejects_0() {
    TEST_ASSERT_FALSE(NVSManager::setDeviceID(0));
    TEST_ASSERT_EQUAL_UINT8(0, NVSManager::getDeviceID());
}

void test_setDeviceID_rejects_255() {
    TEST_ASSERT_FALSE(NVSManager::setDeviceID(255));
    TEST_ASSERT_EQUAL_UINT8(0, NVSManager::getDeviceID());
}

void test_setDeviceID_accepts_1() {
    TEST_ASSERT_TRUE(NVSManager::setDeviceID(1));
    TEST_ASSERT_EQUAL_UINT8(1, NVSManager::getDeviceID());
}

void test_setDeviceID_accepts_254() {
    TEST_ASSERT_TRUE(NVSManager::setDeviceID(254));
    TEST_ASSERT_EQUAL_UINT8(254, NVSManager::getDeviceID());
}

void test_setDeviceID_overwrites_previous() {
    NVSManager::setDeviceID(10);
    NVSManager::setDeviceID(20);
    TEST_ASSERT_EQUAL_UINT8(20, NVSManager::getDeviceID());
}

// ---------------------------------------------------------------------------
// Tests: WiFi SSID
// ---------------------------------------------------------------------------

void test_getWifiSSID_returns_empty_if_not_set() {
    NVSManager::clear();
    String ssid = NVSManager::getWifiSSID();
    TEST_ASSERT_EQUAL_STRING("", ssid.c_str());
}

void test_setWifiSSID_stores_string() {
    const char* test_ssid = "MyNetwork";
    TEST_ASSERT_TRUE(NVSManager::setWifiSSID(test_ssid));
    String ssid = NVSManager::getWifiSSID();
    TEST_ASSERT_EQUAL_STRING(test_ssid, ssid.c_str());
}

void test_setWifiSSID_accepts_32_chars() {
    // Create a 32-character string
    const char* long_ssid = "12345678901234567890123456789012";  // exactly 32
    TEST_ASSERT_TRUE(NVSManager::setWifiSSID(long_ssid));
    String ssid = NVSManager::getWifiSSID();
    TEST_ASSERT_EQUAL_STRING(long_ssid, ssid.c_str());
}

void test_setWifiSSID_rejects_33_chars() {
    // Create a 33-character string
    const char* too_long = "123456789012345678901234567890123";  // 33 chars
    TEST_ASSERT_FALSE(NVSManager::setWifiSSID(too_long));
}

void test_setWifiSSID_rejects_null() {
    TEST_ASSERT_FALSE(NVSManager::setWifiSSID(nullptr));
}

void test_setWifiSSID_overwrites_previous() {
    NVSManager::setWifiSSID("Network1");
    NVSManager::setWifiSSID("Network2");
    String ssid = NVSManager::getWifiSSID();
    TEST_ASSERT_EQUAL_STRING("Network2", ssid.c_str());
}

// ---------------------------------------------------------------------------
// Tests: WiFi Password
// ---------------------------------------------------------------------------

void test_getWifiPassword_returns_empty_if_not_set() {
    NVSManager::clear();
    String pass = NVSManager::getWifiPassword();
    TEST_ASSERT_EQUAL_STRING("", pass.c_str());
}

void test_setWifiPassword_stores_string() {
    const char* test_pass = "MySecurePassword123";
    TEST_ASSERT_TRUE(NVSManager::setWifiPassword(test_pass));
    String pass = NVSManager::getWifiPassword();
    TEST_ASSERT_EQUAL_STRING(test_pass, pass.c_str());
}

void test_setWifiPassword_accepts_64_chars() {
    // Create a 64-character string
    const char* long_pass =
        "1234567890123456789012345678901234567890123456789012345678901234";  // 64
    TEST_ASSERT_TRUE(NVSManager::setWifiPassword(long_pass));
    String pass = NVSManager::getWifiPassword();
    TEST_ASSERT_EQUAL_STRING(long_pass, pass.c_str());
}

void test_setWifiPassword_rejects_65_chars() {
    // Create a 65-character string
    const char* too_long =
        "12345678901234567890123456789012345678901234567890123456789012345";  // 65
    TEST_ASSERT_FALSE(NVSManager::setWifiPassword(too_long));
}

void test_setWifiPassword_rejects_null() {
    TEST_ASSERT_FALSE(NVSManager::setWifiPassword(nullptr));
}

void test_setWifiPassword_overwrites_previous() {
    NVSManager::setWifiPassword("OldPassword");
    NVSManager::setWifiPassword("NewPassword");
    String pass = NVSManager::getWifiPassword();
    TEST_ASSERT_EQUAL_STRING("NewPassword", pass.c_str());
}

// ---------------------------------------------------------------------------
// Tests: Crypto Key (AES-128, 16 bytes)
// ---------------------------------------------------------------------------

void test_getCryptoKey_fails_if_not_set() {
    NVSManager::clear();
    uint8_t key[16];
    TEST_ASSERT_FALSE(NVSManager::getCryptoKey(key));
}

void test_getCryptoKey_rejects_null() {
    TEST_ASSERT_FALSE(NVSManager::getCryptoKey(nullptr));
}

void test_setCryptoKey_rejects_null() {
    TEST_ASSERT_FALSE(NVSManager::setCryptoKey(nullptr));
}

void test_setCryptoKey_stores_16_bytes() {
    uint8_t key_in[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    TEST_ASSERT_TRUE(NVSManager::setCryptoKey(key_in));

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));

    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(key_in[i], key_out[i]);
    }
}

void test_setCryptoKey_all_zeros() {
    uint8_t key_in[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(NVSManager::setCryptoKey(key_in));

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));

    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, key_out[i]);
    }
}

void test_setCryptoKey_all_ones() {
    uint8_t key_in[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                          255, 255, 255, 255, 255, 255, 255, 255};
    TEST_ASSERT_TRUE(NVSManager::setCryptoKey(key_in));

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));

    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(255, key_out[i]);
    }
}

void test_setCryptoKey_overwrites_previous() {
    uint8_t key1[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    uint8_t key2[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};

    NVSManager::setCryptoKey(key1);
    NVSManager::setCryptoKey(key2);

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));

    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(2, key_out[i]);
    }
}

// ---------------------------------------------------------------------------
// Tests: isConfigured()
// ---------------------------------------------------------------------------

void test_isConfigured_false_initially() {
    NVSManager::clear();
    TEST_ASSERT_FALSE(NVSManager::isConfigured());
}

void test_isConfigured_true_after_setDeviceID() {
    NVSManager::clear();
    NVSManager::setDeviceID(5);
    TEST_ASSERT_TRUE(NVSManager::isConfigured());
}

void test_isConfigured_false_after_setDeviceID_0() {
    NVSManager::clear();
    NVSManager::setDeviceID(5);
    TEST_ASSERT_TRUE(NVSManager::isConfigured());
    // Now attempt to set to 0 (should fail, so isConfigured stays true)
    NVSManager::setDeviceID(0);
    TEST_ASSERT_TRUE(NVSManager::isConfigured());  // Device ID still 5
}

void test_isConfigured_only_cares_about_device_id() {
    NVSManager::clear();
    // Set WiFi and crypto key, but no device ID
    NVSManager::setWifiSSID("TestNet");
    uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    NVSManager::setCryptoKey(key);

    // Still not configured without device ID
    TEST_ASSERT_FALSE(NVSManager::isConfigured());
}

// ---------------------------------------------------------------------------
// Tests: clear() / factory reset
// ---------------------------------------------------------------------------

void test_clear_erases_all_config() {
    // Set all fields
    NVSManager::setDeviceID(10);
    NVSManager::setWifiSSID("network");
    NVSManager::setWifiPassword("password");
    uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    NVSManager::setCryptoKey(key);

    // Clear all
    TEST_ASSERT_TRUE(NVSManager::clear());

    // Verify all erased
    TEST_ASSERT_EQUAL_UINT8(0, NVSManager::getDeviceID());
    TEST_ASSERT_EQUAL_STRING("", NVSManager::getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("", NVSManager::getWifiPassword().c_str());

    uint8_t key_out[16];
    TEST_ASSERT_FALSE(NVSManager::getCryptoKey(key_out));
    TEST_ASSERT_FALSE(NVSManager::isConfigured());
}

void test_clear_returns_true() {
    NVSManager::setDeviceID(42);
    TEST_ASSERT_TRUE(NVSManager::clear());
}

// ---------------------------------------------------------------------------
// Tests: Integration scenarios
// ---------------------------------------------------------------------------

void test_full_provisioning_flow() {
    NVSManager::clear();

    // Step 1: Device gets assigned an ID
    TEST_ASSERT_TRUE(NVSManager::setDeviceID(42));
    TEST_ASSERT_TRUE(NVSManager::isConfigured());

    // Step 2: WiFi credentials stored
    TEST_ASSERT_TRUE(NVSManager::setWifiSSID("HomeNetwork"));
    TEST_ASSERT_TRUE(NVSManager::setWifiPassword("SecurePassword123"));

    // Step 3: Encryption key generated and stored
    uint8_t key_in[16] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
                          0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00};
    TEST_ASSERT_TRUE(NVSManager::setCryptoKey(key_in));

    // Verify all persisted
    TEST_ASSERT_EQUAL_UINT8(42, NVSManager::getDeviceID());
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", NVSManager::getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("SecurePassword123", NVSManager::getWifiPassword().c_str());

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(key_in[i], key_out[i]);
    }
}

void test_independent_config_storage() {
    NVSManager::clear();

    // Each config item is independent
    NVSManager::setWifiSSID("Network1");
    TEST_ASSERT_EQUAL_STRING("Network1", NVSManager::getWifiSSID().c_str());
    TEST_ASSERT_FALSE(NVSManager::isConfigured());  // No device ID yet

    NVSManager::setWifiPassword("Pass1");
    TEST_ASSERT_EQUAL_STRING("Network1", NVSManager::getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("Pass1", NVSManager::getWifiPassword().c_str());
    TEST_ASSERT_FALSE(NVSManager::isConfigured());  // Still no device ID

    NVSManager::setDeviceID(99);
    TEST_ASSERT_TRUE(NVSManager::isConfigured());  // Now configured
    TEST_ASSERT_EQUAL_STRING("Network1", NVSManager::getWifiSSID().c_str());
    TEST_ASSERT_EQUAL_STRING("Pass1", NVSManager::getWifiPassword().c_str());
}

void test_persistence_across_reconnects() {
    // Simulate power cycle: set data, "disconnect", re-init, verify data
    NVSManager::setDeviceID(77);
    NVSManager::setWifiSSID("StableNetwork");

    uint8_t key_in[16];
    for (int i = 0; i < 16; i++) key_in[i] = i * 16;
    NVSManager::setCryptoKey(key_in);

    // "Power cycle" — re-init
    NVSManager::init();

    // Verify all data still present
    TEST_ASSERT_EQUAL_UINT8(77, NVSManager::getDeviceID());
    TEST_ASSERT_EQUAL_STRING("StableNetwork", NVSManager::getWifiSSID().c_str());

    uint8_t key_out[16];
    TEST_ASSERT_TRUE(NVSManager::getCryptoKey(key_out));
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(i * 16, key_out[i]);
    }
}
