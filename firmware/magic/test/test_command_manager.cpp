#include <unity.h>
#include "arduino_stubs.h"
#include "WiFi.h"
#include "nvs_manager.h"
#include "../lib/App/command_manager.h"
#include <string>
#include <vector>

// --- Test State ---
static String lastResponse = "";
static bool relayCalled = false;
static uint8_t lastRelay = 0;
static bool lastRelayState = false;

void mockResponseCallback(const String& response) {
    lastResponse = response;
}

void mockRelayCallback(uint8_t relay, bool state) {
    relayCalled = true;
    lastRelay = relay;
    lastRelayState = state;
}

// --- Tests ---

void setUp() {
    lastResponse = "";
    relayCalled = false;
    CommandManager::begin();
    CommandManager::setRelayCallback(mockRelayCallback);
}

void tearDown() {}

void test_command_status() {
    CommandManager::process("STATUS", mockResponseCallback);
    TEST_ASSERT_TRUE(lastResponse.indexOf("STATUS") >= 0);
    TEST_ASSERT_TRUE(lastResponse.indexOf("V3") >= 0);
}

void test_command_relay_on() {
    CommandManager::process("RELAY 1 ON", mockResponseCallback);
    TEST_ASSERT_TRUE(relayCalled);
    TEST_ASSERT_EQUAL_UINT8(1, lastRelay);
    TEST_ASSERT_TRUE(lastRelayState);
}

void test_command_relay_off() {
    CommandManager::process("RELAY 0 OFF", mockResponseCallback);
    TEST_ASSERT_TRUE(relayCalled);
    TEST_ASSERT_EQUAL_UINT8(0, lastRelay);
    TEST_ASSERT_FALSE(lastRelayState);
}

void test_command_help() {
    CommandManager::process("HELP", mockResponseCallback);
    TEST_ASSERT_TRUE(lastResponse.indexOf("Available Commands") >= 0);
}

void test_command_unknown() {
    CommandManager::process("INVALID_CMD", mockResponseCallback);
    TEST_ASSERT_TRUE(lastResponse.indexOf("Unknown command") >= 0);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_status);
    RUN_TEST(test_command_relay_on);
    RUN_TEST(test_command_relay_off);
    RUN_TEST(test_command_help);
    RUN_TEST(test_command_unknown);
    return UNITY_END();
}
