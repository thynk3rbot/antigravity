# LoRaLink v2 Feature Parity Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring LoRaLink v2 firmware to full feature parity with v0.1.0 so all 5 fleet devices can be safely migrated.

**Architecture:** v2 keeps its clean 3-layer design (HAL → Transport → App) but adds back all missing managers as modules within those layers. Each feature is a separate `.h/.cpp` pair in `lib/` under the appropriate layer. No copy-paste from v1 — reimplement cleanly using v2 patterns.

**Tech Stack:** ESP32/ESP32-S3, PlatformIO, Arduino framework, FreeRTOS, RadioLib 6.4.0, Adafruit SSD1306, PubSubClient, ArduinoJson, Preferences (NVS), ArduinoOTA, NimBLE-Arduino, mbedTLS (AES-GCM)

**Reference firmware:** `C:\Users\spw1\Documents\Code\Antigravity\firmware\v1\src\managers\` — read these files to understand what each feature must do, then reimplement cleanly.

**Build command:** `python -m platformio run --environment heltec_v3_node`
**Flash command:** `python -m platformio run --environment heltec_v3_node --target upload --upload-port <IP>`
**Monitor:** `python -m platformio device monitor --port COM18 --baud 115200`
**Test device on bench:** COM18 (Peer2, V3, 172.16.0.26)

---

## Phase -1: Test Harness (Do Before Everything)

### Task T: Test Harness Setup

**Why first:** Every subsequent task needs a way to verify correctness. Native unit tests catch logic bugs without hardware. Integration tests verify the fleet tool still works after each flash.

**Files:**
- Modify: `platformio.ini` — add native test environment
- Create: `test/native/test_control_packet.cpp`
- Create: `test/native/test_mesh_coordinator.cpp`
- Create: `test/native/test_packet_dedup.cpp`
- Create: `test/native/test_message_router.cpp`
- Create: `test/mocks/mock_transport.h` — mock TransportInterface for native tests
- Create: `tools/run_tests.py` — orchestrates native + integration tests

**Step 1: Add native environment to platformio.ini**
```ini
[env:native]
platform = native
build_flags =
    -std=c++17
    -D NATIVE_TEST
    -D ROLE_NODE
    -D RADIO_SX1262
    -D FIRMWARE_VERSION=\"test\"
lib_deps =
    throwtheswitch/Unity @ 2.5.2
test_framework = unity
```

**Step 2: Create mock transport**
```cpp
// test/mocks/mock_transport.h
#pragma once
#include "../../lib/Transport/interface.h"
#include <vector>
#include <cstring>

class MockTransport : public TransportInterface {
public:
    std::vector<std::vector<uint8_t>> sent;
    std::vector<uint8_t> nextRecv;

    bool init() override { return true; }
    int  send(const uint8_t* data, size_t len) override {
        sent.push_back(std::vector<uint8_t>(data, data + len));
        return (int)len;
    }
    int  recv(uint8_t* buf, size_t maxLen) override {
        if (nextRecv.empty()) return 0;
        size_t n = std::min(maxLen, nextRecv.size());
        memcpy(buf, nextRecv.data(), n);
        nextRecv.clear();
        return (int)n;
    }
    bool isReady() const override { return true; }
    void poll() override {}
    TransportType getType() const override { return TransportType::SERIAL_DEBUG; }
};
```

**Step 3: Write ControlPacket tests**
```cpp
// test/native/test_control_packet.cpp
#include <unity.h>
#include "../../lib/App/control_packet.h"

void test_packet_size_is_14_bytes() {
    TEST_ASSERT_EQUAL(14, sizeof(ControlPacket));
}

void test_make_telemetry_sets_type() {
    auto pkt = ControlPacket::makeTelemetry(1, 0xFF, 250, 330, 0x01, 85);
    TEST_ASSERT_EQUAL((uint8_t)PacketType::TELEMETRY, pkt.header.type);
    TEST_ASSERT_EQUAL(1, pkt.header.src);
    TEST_ASSERT_EQUAL(0xFF, pkt.header.dest);
}

void test_make_action_sets_mask() {
    auto pkt = ControlPacket::makeAction(0, 1, 0x03, true, 42);
    TEST_ASSERT_EQUAL((uint8_t)PacketType::ACTION, pkt.header.type);
    TEST_ASSERT_EQUAL(0x03, pkt.payload.action.relayMask);
}

void test_make_ack() {
    auto pkt = ControlPacket::makeACK(0, 1, 7);
    TEST_ASSERT_EQUAL((uint8_t)PacketType::ACK, pkt.header.type);
    TEST_ASSERT_EQUAL(7, pkt.header.seq);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_packet_size_is_14_bytes);
    RUN_TEST(test_make_telemetry_sets_type);
    RUN_TEST(test_make_action_sets_mask);
    RUN_TEST(test_make_ack);
    return UNITY_END();
}
```

**Step 4: Write MeshCoordinator tests** (mock millis() for native)
```cpp
// test/native/test_mesh_coordinator.cpp
// Tests: updateNeighbor, getNextHop (best RSSI), shouldRelay, ageOutNeighbors
// See lib/App/mesh_coordinator.h for API
```

**Step 5: Run native tests**
```bash
cd C:\Users\spw1\Documents\Code\Antigravity\firmware\v2
python -m platformio test --environment native
```
Expected: All tests PASS.

**Step 6: Create tools/run_tests.py — integration test runner**
```python
#!/usr/bin/env python3
"""LoRaLink v2 integration test runner. Run after each flash."""
import subprocess, sys, requests, time

BENCH_IP = "172.16.0.26"
TIMEOUT = 6

def test_http_status():
    r = requests.get(f"http://{BENCH_IP}/api/status", timeout=TIMEOUT)
    assert r.status_code == 200, f"HTTP status {r.status_code}"
    d = r.json()
    assert "id" in d, "Missing 'id' field"
    assert "version" in d, "Missing 'version' field"
    print(f"  HTTP OK: {d['id']} {d['version']}")

def test_build_all():
    for env in ["heltec_v2_hub", "heltec_v3_node", "heltec_v4_node"]:
        r = subprocess.run(
            ["python", "-m", "platformio", "run", "--environment", env],
            capture_output=True, text=True
        )
        assert r.returncode == 0, f"Build failed for {env}:\n{r.stderr[-500:]}"
        print(f"  Build OK: {env}")

if __name__ == "__main__":
    print("=== LoRaLink v2 Integration Tests ===")
    results = []
    for name, fn in [("build_all", test_build_all), ("http_status", test_http_status)]:
        try:
            fn()
            results.append((name, "PASS"))
        except Exception as e:
            results.append((name, f"FAIL: {e}"))
    print("\nResults:")
    for name, status in results:
        print(f"  {'OK' if 'PASS' in status else 'XX'}  {name}: {status}")
    sys.exit(0 if all("PASS" in s for _, s in results) else 1)
```

**Step 7: Commit**
```bash
git add test/ tools/run_tests.py platformio.ini
git commit -m "test: add native unit test harness (Unity) + integration test runner"
```

---

## Phase 0: Critical Bug Fix (Do First)

### Task 0: Fix V3/V4 Boot Loop

**The bug:** `relayHAL.init()` calls `pinMode(12, OUTPUT)` which asserts SX1262 hardware reset → watchdog reboot loop.

**Files:**
- Modify: `lib/HAL/board_config.h` lines 101–123

**Step 1: Verify the bug**
Run monitor on COM18. Confirm output loops at `[RadioHAL] SX1262 initialized` with `rst:0x8 (TG1WDT_SYS_RST)`.

**Step 2: Fix relay pin definitions**

In `board_config.h`, the `#elif defined(RADIO_SX1262)` relay block already has all pins set to 255. Confirm this section exists and is correct:
```c
#elif defined(RADIO_SX1262)
  // V3/V4: SX1262 uses GPIO 8-14 — all relay pins set to 255 (not connected)
  #define RELAY_CH0   255
  #define RELAY_CH1   255
  // ... all 255
```
If the code already shows 255 for all, the bug is elsewhere. Check `relay_hal.cpp` line 36 — the guard must be `if (pin < 255)` not `if (pin != 255)`.

**Step 3: Check lora_transport.cpp init**
The second init in `loraTransport.init()` calls `radioHAL.init()` again. Confirm `_initialized` guard prevents double-init. If `_initialized = false` is being reset anywhere, fix it.

**Step 4: Build and flash to bench device**
```bash
cd C:\Users\spw1\Documents\Code\Antigravity\firmware\v2
python -m platformio run --environment heltec_v3_node --target upload --upload-port 172.16.0.26
```

**Step 5: Verify on serial monitor**
Expected output (no more boot loop):
```
=== LoRaLink v2 Boot ===
[1/6] Initializing HAL...
[RadioHAL] SX1262 initialized
  ✓ HAL initialized
[2/6] Initializing transports...
  ✓ Transport initialized
[3/6] ...
[6/6] Entering main loop
```

**Step 6: Commit**
```bash
git add lib/HAL/board_config.h lib/HAL/relay_hal.cpp
git commit -m "fix: resolve V3/V4 boot loop caused by relay pins conflicting with SX1262 GPIO"
```

---

## Phase 1: Core Infrastructure

### Task 1: Add ArduinoJson + update platformio.ini

**Why:** Almost every feature (WiFi API, BLE, NVS, MQTT) needs JSON serialization.

**Files:**
- Modify: `platformio.ini`

**Step 1: Add ArduinoJson to all environments**

In `platformio.ini`, add to the `[common]` or each environment's `lib_deps`:
```ini
ArduinoJson @ 7.1.0
```

**Step 2: Build to confirm it resolves**
```bash
python -m platformio run --environment heltec_v3_node
```
Expected: Compiles cleanly.

**Step 3: Commit**
```bash
git commit -m "deps: add ArduinoJson 7.1.0 for JSON serialization across all modules"
```

---

### Task 2: NVS Persistence Module

**Why:** Node ID, WiFi credentials, crypto key, link preferences must survive reboot. Everything else depends on this.

**Reference:** `v1/src/managers/DataManager.cpp` — understand what keys it stores, replicate the schema.

**Files:**
- Create: `lib/App/nvs_store.h`
- Create: `lib/App/nvs_store.cpp`

**Step 1: Write the header**
```cpp
// lib/App/nvs_store.h
#pragma once
#include <Preferences.h>
#include <stdint.h>

class NVSStore {
public:
  static NVSStore& getInstance();

  bool init();

  // Node identity
  uint8_t  getNodeID();
  void     setNodeID(uint8_t id);
  String   getNodeName();
  void     setNodeName(const String& name);

  // WiFi
  String   getWiFiSSID();
  String   getWiFiPassword();
  void     setWiFiCredentials(const String& ssid, const String& pass);

  // Crypto
  bool     getCryptoKey(uint8_t* out32);   // 32-byte AES key
  void     setCryptoKey(const uint8_t* key32);

  // Link preference
  uint8_t  getLinkPreference();            // 0=LoRa, 1=WiFi, 2=BLE
  void     setLinkPreference(uint8_t pref);

  // Factory reset
  void     factoryReset();

private:
  NVSStore() = default;
  Preferences _prefs;
  bool _initialized = false;
};

extern NVSStore& nvsStore;
```

**Step 2: Write the implementation**
```cpp
// lib/App/nvs_store.cpp
#include "nvs_store.h"
#include <Arduino.h>

NVSStore& nvsStore = NVSStore::getInstance();

NVSStore& NVSStore::getInstance() {
  static NVSStore instance;
  return instance;
}

bool NVSStore::init() {
  _prefs.begin("loralink", false);
  _initialized = true;
  Serial.println("[NVS] Initialized");
  return true;
}

uint8_t NVSStore::getNodeID() {
  return _prefs.getUChar("node_id", 1);
}
void NVSStore::setNodeID(uint8_t id) {
  _prefs.putUChar("node_id", id);
}

String NVSStore::getNodeName() {
  return _prefs.getString("node_name", "LoRaLink");
}
void NVSStore::setNodeName(const String& name) {
  _prefs.putString("node_name", name);
}

String NVSStore::getWiFiSSID() {
  return _prefs.getString("wifi_ssid", "");
}
String NVSStore::getWiFiPassword() {
  return _prefs.getString("wifi_pass", "");
}
void NVSStore::setWiFiCredentials(const String& ssid, const String& pass) {
  _prefs.putString("wifi_ssid", ssid);
  _prefs.putString("wifi_pass", pass);
}

bool NVSStore::getCryptoKey(uint8_t* out32) {
  size_t len = _prefs.getBytesLength("aes_key");
  if (len != 32) return false;
  _prefs.getBytes("aes_key", out32, 32);
  return true;
}
void NVSStore::setCryptoKey(const uint8_t* key32) {
  _prefs.putBytes("aes_key", key32, 32);
}

uint8_t NVSStore::getLinkPreference() {
  return _prefs.getUChar("link_pref", 0);
}
void NVSStore::setLinkPreference(uint8_t pref) {
  _prefs.putUChar("link_pref", pref);
}

void NVSStore::factoryReset() {
  _prefs.clear();
  Serial.println("[NVS] Factory reset complete");
}
```

**Step 3: Wire into main.cpp setup()**
Add to setup() after Serial.begin():
```cpp
#include "../lib/App/nvs_store.h"
// In setup(), Step 1 before HAL init:
nvsStore.init();
g_ourNodeID = nvsStore.getNodeID();
```

**Step 4: Build**
```bash
python -m platformio run --environment heltec_v3_node
```

**Step 5: Commit**
```bash
git commit -m "feat: add NVS persistence module for node ID, WiFi creds, crypto key"
```

---

### Task 3: Power Management Module

**Reference:** `v1/src/managers/PowerManager.cpp`

**Files:**
- Create: `lib/HAL/power_manager.h`
- Create: `lib/HAL/power_manager.cpp`

**Step 1: Write header**
```cpp
// lib/HAL/power_manager.h
#pragma once
#include <stdint.h>

enum class PowerMode { NORMAL, CONSERVE, CRITICAL };

class PowerManager {
public:
  static PowerManager& getInstance();

  void     init();
  void     update();                  // Call periodically from controlTask

  float    getBatteryVoltage();
  PowerMode getMode() const { return _mode; }
  const char* getModeString() const;

  bool     isVextEnabled() const { return _vextOn; }
  void     setVext(bool on);

private:
  PowerManager() = default;
  PowerMode _mode = PowerMode::NORMAL;
  bool _vextOn = false;
  float _lastVoltage = 0.0f;

  void _updateMode(float voltage);
};

extern PowerManager& powerManager;
```

**Step 2: Implement**
```cpp
// lib/HAL/power_manager.cpp
#include "power_manager.h"
#include "board_config.h"
#include <Arduino.h>

PowerManager& powerManager = PowerManager::getInstance();

PowerManager& PowerManager::getInstance() {
  static PowerManager instance;
  return instance;
}

void PowerManager::init() {
  pinMode(GPIO_VEXT, OUTPUT);
  setVext(true);   // Enable display + LoRa power rail
  Serial.println("[Power] Initialized, VEXT ON");
}

void PowerManager::setVext(bool on) {
  digitalWrite(GPIO_VEXT, on ? LOW : HIGH);   // Active LOW
  _vextOn = on;
}

float PowerManager::getBatteryVoltage() {
  int raw = analogRead(ADC_VBATT);
  _lastVoltage = (raw / 4095.0f) * 3.3f * ADC_VBATT_DIV;
  return _lastVoltage;
}

void PowerManager::_updateMode(float v) {
  if (v < BATTERY_CRIT_VOLTAGE)      _mode = PowerMode::CRITICAL;
  else if (v < BATTERY_WARN_VOLTAGE) _mode = PowerMode::CONSERVE;
  else                                _mode = PowerMode::NORMAL;
}

void PowerManager::update() {
  float v = getBatteryVoltage();
  _updateMode(v);
}

const char* PowerManager::getModeString() const {
  switch (_mode) {
    case PowerMode::NORMAL:   return "NORMAL";
    case PowerMode::CONSERVE: return "CONSERVE";
    case PowerMode::CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
  }
}
```

**Step 3: Add to main.cpp**

In setup() Step 1 (HAL init), add:
```cpp
powerManager.init();
```

In controlTask, add periodic call:
```cpp
powerManager.update();
```

**Step 4: Build and commit**
```bash
python -m platformio run --environment heltec_v3_node
git commit -m "feat: add power management module with VEXT control and 3-tier battery modes"
```

---

## Phase 2: Display

### Task 4: OLED Display Manager

**Reference:** `v1/src/managers/DisplayManager.cpp` — 4-page rotational UI.

**Files:**
- Create: `lib/HAL/display_manager.h`
- Create: `lib/HAL/display_manager.cpp`
- Modify: `platformio.ini` — confirm Adafruit SSD1306 @ 2.5.0 is present

**Step 1: Write header**
```cpp
// lib/HAL/display_manager.h
#pragma once
#include <stdint.h>

class DisplayManager {
public:
  static DisplayManager& getInstance();

  bool init();
  void update();            // Call every 2s from controlTask

  // Page control
  void nextPage();
  void showBootMessage(const char* msg);
  void showError(const char* msg);

private:
  DisplayManager() = default;
  bool _initialized = false;
  uint8_t _page = 0;
  uint32_t _lastUpdate = 0;

  void _drawHome();
  void _drawNetwork();
  void _drawStatus();
  void _drawLog();
};

extern DisplayManager& displayManager;
```

**Step 2: Implement (key pages)**
```cpp
// lib/HAL/display_manager.cpp
#include "display_manager.h"
#include "board_config.h"
#include "power_manager.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

static Adafruit_SSD1306 _oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

DisplayManager& displayManager = DisplayManager::getInstance();

DisplayManager& DisplayManager::getInstance() {
  static DisplayManager instance;
  return instance;
}

bool DisplayManager::init() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[Display] SSD1306 not found");
    return false;
  }
  _oled.clearDisplay();
  _oled.setTextSize(1);
  _oled.setTextColor(SSD1306_WHITE);
  _initialized = true;
  Serial.println("[Display] Initialized");
  return true;
}

void DisplayManager::showBootMessage(const char* msg) {
  if (!_initialized) return;
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.println("LoRaLink v2");
  _oled.println(msg);
  _oled.display();
}

void DisplayManager::update() {
  if (!_initialized) return;
  uint32_t now = millis();
  if (now - _lastUpdate < 2000) return;
  _lastUpdate = now;
  switch (_page % 4) {
    case 0: _drawHome(); break;
    case 1: _drawNetwork(); break;
    case 2: _drawStatus(); break;
    case 3: _drawLog(); break;
  }
}

void DisplayManager::nextPage() { _page++; }

void DisplayManager::_drawHome() {
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.printf("LoRaLink v2\n");
  _oled.printf("Bat: %.2fV %s\n",
    powerManager.getBatteryVoltage(),
    powerManager.getModeString());
  _oled.display();
}

// _drawNetwork, _drawStatus, _drawLog: similar pattern
// See v1 DisplayManager.cpp for full content to port
void DisplayManager::_drawNetwork() {
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.println("[ Network ]");
  _oled.display();
}

void DisplayManager::_drawStatus() {
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.printf("Up: %lus\n", millis()/1000);
  _oled.display();
}

void DisplayManager::_drawLog() {
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.println("[ Log ]");
  _oled.display();
}

void DisplayManager::showError(const char* msg) {
  if (!_initialized) return;
  _oled.clearDisplay();
  _oled.setCursor(0, 0);
  _oled.println("ERROR:");
  _oled.println(msg);
  _oled.display();
}
```

**Step 3: Wire into main.cpp**
```cpp
#include "../lib/HAL/display_manager.h"
// In setup() Step 1:
displayManager.init();
displayManager.showBootMessage("Booting...");
// In controlTask loop:
displayManager.update();
```

**Step 4: Build and flash, verify OLED shows "LoRaLink v2"**

**Step 5: Commit**
```bash
git commit -m "feat: add OLED display manager with 4-page rotational UI"
```

---

## Phase 3: Connectivity

### Task 5: WiFi Transport + HTTP API

**Reference:** `v1/src/managers/WiFiManager.cpp` (2175 lines) — this is the largest port.

**Files:**
- Create: `lib/Transport/wifi_transport.h`
- Create: `lib/Transport/wifi_transport.cpp`

**Step 1: Write header**
```cpp
// lib/Transport/wifi_transport.h
#pragma once
#include "interface.h"
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

class WiFiTransport : public TransportInterface {
public:
  static WiFiTransport& getInstance();

  bool init() override;
  int  send(const uint8_t* data, size_t len) override;
  int  recv(uint8_t* buf, size_t maxLen) override;
  bool isReady() const override;
  void poll() override;          // Call from loop/task — handles HTTP + OTA

  TransportType getType() const override { return TransportType::WIFI; }

  // HTTP API helpers (used by web handlers)
  String getStatusJson();
  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  String getIP() const { return WiFi.localIP().toString(); }

private:
  WiFiTransport() = default;
  bool _initialized = false;
  WebServer* _server = nullptr;

  void _connectWiFi();
  void _setupRoutes();

  // HTTP route handlers
  void _handleStatus();
  void _handleConfig();
  void _handleRoot();
  void _handleSetWiFi();
  void _handleReboot();
  void _handleNotFound();
};

extern WiFiTransport& wifiTransport;
```

**Step 2: Implement core WiFi + /api/status**
```cpp
// lib/Transport/wifi_transport.cpp
#include "wifi_transport.h"
#include "../App/nvs_store.h"
#include "../HAL/power_manager.h"
#include <ArduinoJson.h>
#include <Arduino.h>

WiFiTransport& wifiTransport = WiFiTransport::getInstance();

WiFiTransport& WiFiTransport::getInstance() {
  static WiFiTransport instance;
  return instance;
}

bool WiFiTransport::init() {
  String ssid = nvsStore.getWiFiSSID();
  String pass = nvsStore.getWiFiPassword();

  if (ssid.isEmpty()) {
    Serial.println("[WiFi] No credentials stored, skipping");
    return false;
  }

  _connectWiFi();

  _server = new WebServer(80);
  _setupRoutes();
  _server->begin();

  // OTA
  ArduinoOTA.setHostname(nvsStore.getNodeName().c_str());
  ArduinoOTA.begin();

  _initialized = true;
  Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void WiFiTransport::_connectWiFi() {
  String ssid = nvsStore.getWiFiSSID();
  String pass = nvsStore.getWiFiPassword();
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
    delay(250);
  }
}

void WiFiTransport::poll() {
  if (!_initialized) return;
  if (_server) _server->handleClient();
  ArduinoOTA.handle();
}

String WiFiTransport::getStatusJson() {
  JsonDocument doc;
  doc["id"]          = nvsStore.getNodeName();
  doc["version"]     = FIRMWARE_VERSION;
  doc["uptime"]      = String(millis() / 1000) + "s";
  doc["ip"]          = getIP();
  doc["wifi"]        = isConnected();
  doc["bat"]         = powerManager.getBatteryVoltage();
  doc["power_mode"]  = powerManager.getModeString();
  doc["radio_state"] = "rx";
  String out;
  serializeJson(doc, out);
  return out;
}

void WiFiTransport::_setupRoutes() {
  _server->on("/",            [this]{ _handleRoot(); });
  _server->on("/api/status",  [this]{ _handleStatus(); });
  _server->on("/api/config",  [this]{ _handleConfig(); });
  _server->on("/api/reboot",  [this]{ _handleReboot(); });
  _server->on("/api/setwifi", HTTP_POST, [this]{ _handleSetWiFi(); });
  _server->onNotFound(        [this]{ _handleNotFound(); });
}

void WiFiTransport::_handleStatus() {
  _server->send(200, "application/json", getStatusJson());
}

void WiFiTransport::_handleRoot() {
  // Minimal dashboard — expand with full v1 HTML from WiFiManager.cpp serveHome()
  _server->send(200, "text/html",
    "<html><body><h1>LoRaLink v2</h1>"
    "<a href='/api/status'>Status JSON</a></body></html>");
}

void WiFiTransport::_handleConfig() {
  _server->send(200, "application/json", "{\"todo\":\"config\"}");
}

void WiFiTransport::_handleSetWiFi() {
  if (_server->hasArg("ssid") && _server->hasArg("pass")) {
    nvsStore.setWiFiCredentials(_server->arg("ssid"), _server->arg("pass"));
    _server->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
  }
  _server->send(400, "application/json", "{\"error\":\"missing ssid/pass\"}");
}

void WiFiTransport::_handleReboot() {
  _server->send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void WiFiTransport::_handleNotFound() {
  _server->send(404, "text/plain", "Not found");
}

// TransportInterface stubs (WiFi transport doesn't carry LoRa-style packets)
int WiFiTransport::send(const uint8_t*, size_t) { return 0; }
int WiFiTransport::recv(uint8_t*, size_t) { return 0; }
bool WiFiTransport::isReady() const { return isConnected(); }
```

**Step 3: Wire into main.cpp**
```cpp
#include "../lib/Transport/wifi_transport.h"
// In setup() Step 2 (transports), ROLE_HUB only:
#ifdef ROLE_HUB
  wifiTransport.init();
  messageRouter.registerTransport(&wifiTransport);
#endif
// In main loop() or controlTask:
wifiTransport.poll();
```

**Step 4: Add WebServer to platformio.ini**
WebServer is part of the ESP32 Arduino core — no extra lib_dep needed.

**Step 5: Flash and verify**
```bash
python -m platformio run --environment heltec_v3_node --target upload --upload-port 172.16.0.26
```
Then: `curl http://172.16.0.26/api/status` — should return JSON.

**Step 6: Commit**
```bash
git commit -m "feat: add WiFi transport with HTTP API (/api/status), OTA support, config endpoints"
```

---

### Task 6: Expand /api/status to match v0.1.0 response

**Why:** `loralink_status.py` tool and the web dashboard depend on the full status object.

**Step 1:** Compare `getStatusJson()` output against actual v0.1.0 response fields from the live fleet. Run:
```bash
python tools\loralink_status.py 172.16.0.27 --json
```
Note all fields present in v0.1.0 response.

**Step 2:** Add missing fields to `getStatusJson()` in `wifi_transport.cpp`:
- `lora_tx`, `lora_rx`, `lora_drop`, `lora_toa_ms` — from RadioHAL stats
- `heap` — `ESP.getFreeHeap()`
- `ble`, `espnow` — bool flags
- `nodes` — `meshCoordinator.getNeighborCount()`
- `last_cmd` — add a global `g_lastCmd` string updated by CommandManager
- `reset` — `esp_reset_reason()` string
- `hw` — last 6 chars of MAC address
- `log` — last N messages from a ring buffer

**Step 3: Build, flash, run `loralink_status.py` — confirm table fills correctly**

**Step 4: Commit**
```bash
git commit -m "feat: expand /api/status to full v0.1.0 schema for fleet tool compatibility"
```

---

### Task 7: BLE Transport (NUS Terminal)

**Reference:** `v1/src/managers/BLEManager.cpp`

**Files:**
- Create: `lib/Transport/ble_transport.h`
- Create: `lib/Transport/ble_transport.cpp`
- Modify: `platformio.ini` — add `NimBLE-Arduino @ 1.4.2`

**Step 1: Add NimBLE-Arduino to platformio.ini**
```ini
NimBLE-Arduino @ 1.4.2
```

**Step 2: Write header**
```cpp
// lib/Transport/ble_transport.h
#pragma once
#include "interface.h"
#include <NimBLEDevice.h>

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

class BLETransport : public TransportInterface {
public:
  static BLETransport& getInstance();

  bool init() override;
  int  send(const uint8_t* data, size_t len) override;  // Notify to connected client
  int  recv(uint8_t* buf, size_t maxLen) override;
  bool isReady() const override;
  void poll() override;

  TransportType getType() const override { return TransportType::BLE; }

  bool isConnected() const { return _connected; }
  void sendString(const String& s);

private:
  BLETransport() = default;
  bool _initialized = false;
  bool _connected = false;
  NimBLECharacteristic* _txChar = nullptr;
  NimBLECharacteristic* _rxChar = nullptr;

  // RX ring buffer
  static constexpr size_t RX_BUF = 512;
  uint8_t _rxBuf[RX_BUF];
  size_t _rxHead = 0, _rxTail = 0;

  friend class _BLEServerCallbacks;
  friend class _BLERxCallbacks;
};

extern BLETransport& bleTransport;
```

**Step 3: Implement**
Key parts to implement:
- `NimBLEDevice::init("GW-" + nodeName)` — matches v1 naming scheme
- Server callbacks to set `_connected` flag
- RX characteristic callback: push received bytes to `_rxBuf`
- `send()` calls `_txChar->setValue()` then `notify()`
- `recv()` drains `_rxBuf` into provided buffer

See `v1/src/managers/BLEManager.cpp` for exact callback structure.

**Step 4: Wire into main.cpp**
```cpp
#include "../lib/Transport/ble_transport.h"
// In setup() Step 2:
bleTransport.init();
messageRouter.registerTransport(&bleTransport);
// In loop/task:
bleTransport.poll();
```

**Step 5: Verify with existing tool**
```bash
python tools\loralink_status.py --scan-ble
```
New device should appear as `GW-<NodeName>`.

**Step 6: Commit**
```bash
git commit -m "feat: add BLE NUS transport — matches v0.1.0 GW-* naming, compatible with ble_instrument.py"
```

---

### Task 8: MQTT Transport

**Reference:** `v1/src/managers/MQTTManager.cpp`

**Files:**
- Modify: `lib/Transport/mqtt_transport.h` (currently stub)
- Create: `lib/Transport/mqtt_transport.cpp`

**Key topics to publish (match v0.1.0 exactly):**
- `loralink/<nodeID>/status` → full status JSON
- `loralink/<nodeID>/telemetry` → battery, RSSI, uptime
- `loralink/<nodeID>/log` → last command/message

**Subscribe:**
- `loralink/<nodeID>/cmd` → incoming commands

Wire after WiFi connects. Reconnect with backoff if disconnected.

**Step 1: Implement following v1 MQTTManager.cpp pattern**
**Step 2: Add MQTT broker config to NVSStore (host, port, user, pass)**
**Step 3: Build, flash, verify with MQTT client**
**Step 4: Commit**
```bash
git commit -m "feat: add MQTT transport for telemetry and remote commands (Hub-only)"
```

---

## Phase 4: Command Layer

### Task 9: Serial + Command Manager

**Reference:** `v1/src/managers/CommandManager.cpp`

**Files:**
- Create: `lib/App/command_manager.h`
- Create: `lib/App/command_manager.cpp`

**Step 1: Write command registry pattern**
```cpp
// lib/App/command_manager.h
#pragma once
#include <functional>
#include <map>
#include <Arduino.h>
#include "../Transport/interface.h"

using CommandHandler = std::function<void(const String& args, TransportType source)>;

class CommandManager {
public:
  static CommandManager& getInstance();

  void init();
  void poll();       // Read Serial, dispatch
  void registerCommand(const String& name, CommandHandler handler);
  void dispatch(const String& cmd, TransportType source);
  void sendResponse(const String& msg, TransportType source);

private:
  CommandManager() = default;
  std::map<String, CommandHandler> _commands;
  String _serialBuf;

  void _registerBuiltins();
};

extern CommandManager& commandManager;
```

**Step 2: Implement built-in commands** (match v0.1.0 exactly):
- `STATUS` → send status string to source
- `BLINK` → trigger LED blink
- `READMAC` → return WiFi MAC
- `SETNAME <name>` → save to NVS
- `SETWIFI <ssid> <pass>` → save to NVS, reboot
- `RELAY <ch> ON|OFF` → call relayHAL.setChannel()
- `RESET` → ESP.restart()
- `HELP` → list commands

**Step 3: Add poll() call to main loop**
**Step 4: Wire BLE transport → CommandManager dispatch**
When BLE receives a line ending `\n`, call `commandManager.dispatch(line, TransportType::BLE)`

**Step 5: Build, flash, test via serial monitor**
Type `STATUS` → should see response.

**Step 6: Commit**
```bash
git commit -m "feat: add command manager with STATUS, RELAY, SETWIFI, BLINK, HELP built-in commands"
```

---

## Phase 5: LoRa Completion

### Task 10: AES-128-GCM Encryption

**Reference:** `v1/src/managers/LoRaManager.cpp` — find `encrypt()` / `decrypt()` implementation.

**Files:**
- Modify: `lib/Transport/lora_transport.cpp` — implement `_encryptPacket()` / `_decryptPacket()`

**Step 1: Check what AES library v1 uses**
```bash
grep -r "AES\|aes\|mbedtls\|Crypto" /c/Users/spw1/Documents/Code/Antigravity/firmware/v1/platformio.ini
grep -r "#include.*aes\|#include.*mbedtls" /c/Users/spw1/Documents/Code/Antigravity/firmware/v1/src/
```

**Step 2: Use mbedTLS (built into ESP-IDF)**
```cpp
#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>

bool LoRaTransport::_encryptPacket(uint8_t* buf, size_t* len) {
  uint8_t key[32];
  if (!nvsStore.getCryptoKey(key)) return false;  // No key = no encrypt

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);

  uint8_t iv[12];
  esp_fill_random(iv, sizeof(iv));

  uint8_t tag[16];
  uint8_t ciphertext[256];
  mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
    *len, iv, sizeof(iv), nullptr, 0,
    buf, ciphertext, sizeof(tag), tag);

  // Pack: [IV(12)] [TAG(16)] [CIPHERTEXT(*len)]
  memcpy(buf, iv, 12);
  memcpy(buf + 12, tag, 16);
  memcpy(buf + 28, ciphertext, *len);
  *len += 28;

  mbedtls_gcm_free(&ctx);
  return true;
}
```

**Step 3: Implement `_decryptPacket()` (reverse)**

**Step 4: Encryption is opt-in** — if no key stored in NVS, skip encryption (plain mode for backwards compat with v0.1.0 devices during migration)

**Step 5: Build, test encrypt/decrypt round-trip in bench mode**

**Step 6: Commit**
```bash
git commit -m "feat: implement AES-128-GCM encryption in LoRa transport using mbedTLS"
```

---

### Task 11: Reliable Delivery (ACK Queue)

**Reference:** LoRaManager.cpp pending-ACK queue logic.

**Files:**
- Modify: `lib/Transport/lora_transport.h` — add `_pendingAcks` map
- Modify: `lib/Transport/lora_transport.cpp` — add retry logic

**Key behavior:**
- On `send()` for packets with `FLAG_REQUIRES_ACK`: add to pending map keyed by seq
- On `recv()` ACK packet: remove from pending map
- In `poll()`: retry pending packets older than 3s, up to 3 attempts, then drop

**Step 1: Add retry map to LoRaTransport**
**Step 2: Implement retry in poll()**
**Step 3: Test: send packet with ACK required, verify retransmit if no ACK received**
**Step 4: Commit**

---

### Task 12: Heartbeat & Neighbor Keep-Alive

**Files:**
- Modify: `src/main.cpp` — add heartbeat TX in controlTask

**Step 1: In controlTask, send HEARTBEAT packet every `HEARTBEAT_NORMAL_MS`**
```cpp
static uint32_t lastHeartbeat = 0;
uint32_t hbInterval = (powerManager.getMode() == PowerMode::NORMAL)
                      ? HEARTBEAT_NORMAL_MS : HEARTBEAT_CONSERVE_MS;
if (now - lastHeartbeat >= hbInterval) {
  ControlPacket hb = ControlPacket::makeHeartbeat(g_ourNodeID, 0xFF);
  messageRouter.broadcastPacket((uint8_t*)&hb, sizeof(hb));
  lastHeartbeat = now;
}
```

**Step 2: Commit**
```bash
git commit -m "feat: add periodic heartbeat transmission with power-mode-aware interval"
```

---

## Phase 6: Telemetry

### Task 13: Real Telemetry Collection (ADC Sampling)

**Files:**
- Modify: `src/main.cpp` — replace hardcoded values with real ADC reads

**Step 1: Replace placeholders in controlTask**
```cpp
// Replace:
uint16_t tempC_x10 = 2500;
uint16_t voltageV_x100 = 3300;

// With:
float v = powerManager.getBatteryVoltage();
uint16_t voltageV_x100 = (uint16_t)(v * 100);
// Temperature: ESP32-S3 internal sensor
temperature_sensor_handle_t tsens;
temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
temperature_sensor_install(&tsens_cfg, &tsens);
temperature_sensor_enable(tsens);
float degC;
temperature_sensor_get_celsius(tsens, &degC);
uint16_t tempC_x10 = (uint16_t)(degC * 10);
```

**Step 2: Build for ESP32-S3 target only** — V2 (ESP32) uses different temp sensor API.

**Step 3: Commit**

---

## Phase 7: Scheduling & Products

### Task 14: Schedule Manager

**Reference:** `v1/src/managers/ScheduleManager.cpp` (1093 lines) — largest port after WiFiManager.

This task is optional for MVP. Implement if scheduling/automation features are needed:
- Dynamic task pool (toggle/pulse GPIO at intervals)
- Load from JSON stored in NVS/LittleFS
- Safety thresholds (min/max pulse width)

**See v1/src/managers/ScheduleManager.cpp for full implementation to port.**

---

## Phase 8: Version Bump & OTA Deploy

### Task 15: Version to v0.3.0 and OTA flash fleet

**Step 1: Update version in board_config.h**
```c
#define FIRMWARE_VERSION "0.3.0"
```

**Step 2: Build all three environments**
```bash
python -m platformio run --environment heltec_v2_hub
python -m platformio run --environment heltec_v3_node
python -m platformio run --environment heltec_v4_node
```
All must compile cleanly.

**Step 3: Flash bench device first (COM18 / 172.16.0.26)**
```bash
python -m platformio run --environment heltec_v3_node --target upload --upload-port 172.16.0.26
```

**Step 4: Verify full boot + all features on bench device**
- Serial monitor: all 6 boot steps complete, no crashes
- `python tools\loralink_status.py 172.16.0.26` — full table filled
- `python tools\loralink_status.py --scan-ble` — device visible
- HTTP: `curl http://172.16.0.26/api/status` — matches v0.1.0 schema

**Step 5: Flash remaining devices**
```bash
# V3 nodes
python -m platformio run --environment heltec_v3_node --target upload --upload-port 172.16.0.27
python -m platformio run --environment heltec_v3_node --target upload --upload-port 172.16.0.26

# V4 nodes (use v4 env)
python -m platformio run --environment heltec_v4_node --target upload --upload-port 172.16.0.28
python -m platformio run --environment heltec_v4_node --target upload --upload-port 172.16.0.29

# V2 Hub (via COM7)
python -m platformio run --environment heltec_v2_hub --target upload --upload-port COM7
```

**Step 6: Run fleet status check**
```bash
python tools\loralink_status.py --range 172.16.0.26-30 COM7 COM18
```
Expected: All 5 devices online, all showing v0.3.0.

**Step 7: Final commit + tag**
```bash
git add -A
git commit -m "release: LoRaLink v0.3.0 — full feature parity with v0.1.0 on clean v2 architecture"
git tag v0.3.0
```

---

## Quick Reference

| Task | Feature | Priority | Est. LOC |
|------|---------|----------|----------|
| 0 | Boot loop fix | **CRITICAL** | ~5 |
| 1 | ArduinoJson dep | **CRITICAL** | ~5 |
| 2 | NVS persistence | HIGH | ~80 |
| 3 | Power management | HIGH | ~60 |
| 4 | OLED display | HIGH | ~100 |
| 5 | WiFi + HTTP API | HIGH | ~200 |
| 6 | Full /api/status | HIGH | ~50 |
| 7 | BLE NUS | HIGH | ~150 |
| 8 | MQTT | MEDIUM | ~100 |
| 9 | Command manager | MEDIUM | ~150 |
| 10 | AES encryption | MEDIUM | ~80 |
| 11 | ACK queue | MEDIUM | ~60 |
| 12 | Heartbeat | LOW | ~20 |
| 13 | Real telemetry | LOW | ~30 |
| 14 | Scheduler | OPTIONAL | ~400 |
| 15 | v0.3.0 OTA deploy | FINAL | — |

**Total estimated:** ~1,500 lines of new code + tests
