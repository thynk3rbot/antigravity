#include "CommandManager.h"
#include "../crypto.h"
#include "../utils/DebugMacros.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "ESPNowManager.h"
#include "LoRaManager.h"
#include "ScheduleManager.h"
#include <Arduino.h>
#include <WiFi.h>

// Pin Lookup Table
struct PinMap {
  const char *mnemonic;
  int pin;
};

const PinMap PIN_LOOKUP[] = {{"LED", PIN_LED_BUILTIN},
                             {"PRG", PIN_BUTTON_PRG},
                             {"BAT", PIN_BAT_ADC},
                             {"VEXT", PIN_VEXT_CTRL},
                             {"IO35", 35},
                             {"IO0", 0},
                             {"IO26", 26},
                             {"IO48", 48},
                             {"IO47", 47},
                             {"IO33", 33},
                             {"IO34", 34},
                             {NULL, 0}};

const char *CommandManager::interfaceName(CommInterface ifc) {
  switch (ifc) {
  case CommInterface::COMM_SERIAL:
    return "Serial";
  case CommInterface::COMM_LORA:
    return "LoRa";
  case CommInterface::COMM_BLE:
    return "BLE";
  case CommInterface::COMM_WIFI:
    return "WiFi";
  case CommInterface::COMM_ESPNOW:
    return "ESP-NOW";
  case CommInterface::COMM_INTERNAL:
    return "Internal";
  default:
    return "Unknown";
  }
}

int CommandManager::getPinFromName(const String &name) {
  String upperName = name;
  upperName.toUpperCase();
  upperName.trim();

  for (int i = 0; PIN_LOOKUP[i].mnemonic != NULL; i++) {
    if (upperName == PIN_LOOKUP[i].mnemonic) {
      return PIN_LOOKUP[i].pin;
    }
  }
  if (isdigit(upperName.charAt(0))) {
    return upperName.toInt();
  }
  return -1;
}

void CommandManager::restoreHardwareState() {
  Serial.println("CMD: Restore Hardware START");
  Serial.flush();
  DataManager &data = DataManager::getInstance();
  LOG_PRINTLN("SYS: Restoring Relay/LED States...");

  const char *outputs[] = {"RELAY1", "RELAY2", "RELAY3",
                           "RELAY4", "LED",    "VEXT"};
  int outPins[] = {PIN_RELAY_110V,  PIN_RELAY_12V_1, PIN_RELAY_12V_2,
                   PIN_RELAY_12V_3, PIN_LED_BUILTIN, PIN_VEXT_CTRL};

  for (int i = 0; i < 6; i++) {
    bool state = data.GetGpioState(outputs[i]);
    if (strcmp(outputs[i], "VEXT") == 0)
      state = true;
    pinMode(outPins[i], OUTPUT);
    digitalWrite(outPins[i], state ? HIGH : LOW);
    LOG_PRINTF("  %s -> %s\n", outputs[i], state ? "ON" : "OFF");
    Serial.flush();
  }
  Serial.println("CMD: Restore Hardware OK");
  Serial.flush();
}

void CommandManager::handleCommand(const String &fullCmdIn,
                                   CommInterface source) {
  String fullCmd = fullCmdIn;
  fullCmd.trim();
  if (fullCmd.length() == 0 || fullCmd.length() > 256)
    return;

  bool fromLoRa = (source == CommInterface::COMM_LORA);
  bool fromRemote = (source == CommInterface::COMM_LORA ||
                     source == CommInterface::COMM_ESPNOW);

  DataManager &data = DataManager::getInstance();
  DisplayManager &display = DisplayManager::getInstance();
  LoRaManager &lora = LoRaManager::getInstance();

  display.SetDisplayActive(true);

  const char *sourceStr = interfaceName(source);
  DataManager::getInstance().LogMessage(sourceStr, 0, fullCmd);
  LOG_PRINTF("CMD: [%s] via %s\n", fullCmd.c_str(), sourceStr);

  // "CMD:" prefix legacy support
  if (fullCmd.startsWith("CMD:")) {
    fullCmd = fullCmd.substring(4);
    fullCmd.trim();
  }

  // --- GLOBAL / UNTARGETED COMMAND ROUTING ---
  // If the command doesn't have a space, or its first word isn't a node ID/MAC,
  // check the registry.
  int space = fullCmd.indexOf(' ');
  String potentialCmd = (space > 0) ? fullCmd.substring(0, space) : fullCmd;

  auto it = _commandRegistry.find(potentialCmd);
  if (it != _commandRegistry.end()) {
    // It's a registered untargeted/global command (e.g. SETWIFI, STATUS...)
    String args = (space > 0) ? fullCmd.substring(space + 1) : "";
    it->second(args, source);
    return;
  }

  // --- TARGETED COMMANDS ---
  if (space > 0) {
    String target = fullCmd.substring(0, space);
    String subCmd = fullCmd.substring(space + 1);

    if (target.equalsIgnoreCase("LED") || target.equalsIgnoreCase("GPIO") ||
        target.equalsIgnoreCase("READ")) {
      executeLocalCommand(subCmd, source);
      return;
    }

    if (target.equalsIgnoreCase(data.myId) ||
        target.equalsIgnoreCase(data.getMacSuffix()) ||
        target.equalsIgnoreCase("ALL")) {
      executeLocalCommand(subCmd, source);
      if (fromLoRa && !target.equalsIgnoreCase("ALL")) {
        lora.SendLoRa("ACK: " + subCmd);
      }
      // Forward to other interfaces if from local
      if (!fromRemote) {
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
        lora.SendLoRa(fullCmd);
      }
    } else {
      // Not for us - forward to network using reliable ACK queue
      if (!fromRemote) {
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
        lora.QueueReliableCommand(target, subCmd);
      }
    }
  } else {
    if (!fromRemote) {
      if (fullCmd.equalsIgnoreCase("HELP") ||
          fullCmd.equalsIgnoreCase("STATUS") ||
          fullCmd.equalsIgnoreCase("BLINK") ||
          fullCmd.equalsIgnoreCase("READMAC") ||
          fullCmd.equalsIgnoreCase("RADIO") ||
          strncasecmp(fullCmd.c_str(), "LED", 3) == 0 ||
          strncasecmp(fullCmd.c_str(), "READ", 4) == 0) {
        executeLocalCommand(fullCmd, source);
      } else {
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
        lora.SendLoRa(fullCmd);
      }
    }
  }
}

CommandManager::CommandManager() { initRegistry(); }

void CommandManager::registerCommand(const String &cmd,
                                     CommandHandler handler) {
  String upperCmd = cmd;
  upperCmd.toUpperCase();
  _commandRegistry[upperCmd] = handler;
}

void CommandManager::initRegistry() {
  registerCommand("LED", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.equalsIgnoreCase("ON")) {
      digitalWrite(PIN_LED_BUILTIN, HIGH);
      data.SetGpioState("LED", true);
      lora.lastMsgReceived = "SYS: LED ON";
    } else if (args.equalsIgnoreCase("OFF")) {
      digitalWrite(PIN_LED_BUILTIN, LOW);
      data.SetGpioState("LED", false);
      lora.lastMsgReceived = "SYS: LED OFF";
    }
  });

  registerCommand("STATUS", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    float bat = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                : "DISCONNECTED";
    String msg =
        "ID: " + data.myId + " (HW: [" + data.getMacSuffix() + "]) " +
        "VER: " FIRMWARE_VERSION " IP: " + ip + " BAT: " + String(bat, 2) +
        "V LoRa: " + String(lora.lastRssi) + "dBm EN:" +
        String(ESPNowManager::getInstance().espNowActive ? "ON" : "OFF");
    if (source == CommInterface::COMM_LORA)
      lora.SendLoRa(data.myId + " " + msg);
    else
      LOG_PRINTLN(msg);
    lora.lastMsgReceived = "SYS: STATUS SENT";
  });

  registerCommand("BLINK", [](const String &args, CommInterface source) {
    ScheduleManager::getInstance().triggerBlink();
    LoRaManager::getInstance().lastMsgReceived = "SYS: BLINK";
  });

  registerCommand("READMAC", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    String mac = WiFi.macAddress();
    data.LogMessage("READMAC", 0, "MAC: " + mac);
    if (source == CommInterface::COMM_LORA)
      lora.SendLoRa("MAC: " + mac);
    else
      LOG_PRINTLN("MAC: " + mac);
    lora.lastMsgReceived = "SYS: MAC SENT";
  });

  registerCommand("RADIO", [](const String &args, CommInterface source) {
    LoRaManager &lora = LoRaManager::getInstance();
    lora.DumpDiagnostics();
    lora.lastMsgReceived = "SYS: RADIO DIAG SENT";
  });

  registerCommand("HELP", [](const String &args, CommInterface source) {
    LoRaManager &lora = LoRaManager::getInstance();
    String help = "LED ON/OFF, BLINK, STATUS, READMAC, RADIO, SETNAME, "
                  "SETWIFI, ESPNOW ON/OFF, STREAM ON/OFF";
    if (source == CommInterface::COMM_LORA)
      lora.SendLoRa(help);
    else
      LOG_PRINTLN(help);
  });

  registerCommand("STREAM", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.equalsIgnoreCase("ON") || args == "1") {
      data.streamToSerial = true;
      String msg = "SYS: Serial Stream ON";
      lora.lastMsgReceived = msg;
      Serial.println("TYPE,NODE,BATTERY_V,RSSI,HOPS,MESSAGE");
    } else if (args.equalsIgnoreCase("OFF") || args == "0") {
      data.streamToSerial = false;
      String msg = "SYS: Serial Stream OFF";
      lora.lastMsgReceived = msg;
      LOG_PRINTLN(msg);
    }
  });

  // --- REPLACED GLOBALS ---

  registerCommand("SETNAME", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.length() > 0 && args.length() < 15) {
      data.SetName(args);
      lora.lastMsgReceived = "SYS: Named " + args;
      LOG_PRINTLN("SETNAME -> " + args);
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
    } else {
      lora.lastMsgReceived = "ERR: Name 1-14 chars";
    }
  });

  registerCommand("SLEEP", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    float hours = args.toFloat();
    if (hours > 0.0 && hours <= 24.0) {
      uint64_t sleepUs = (uint64_t)(hours * 3600.0 * 1000000.0);
      unsigned int mins = (unsigned int)(hours * 60.0);
      lora.lastMsgReceived = "SYS: Sleep " + String(mins) + "min";
      lora.SendLoRa(data.myId + " sleeping for " + String(mins) + "min");
      delay(500);
      digitalWrite(PIN_LED_BUILTIN, LOW);
      Heltec.display->displayOff();
      esp_sleep_enable_timer_wakeup(sleepUs);
      esp_deep_sleep_start();
    } else {
      lora.lastMsgReceived = "ERR: SLEEP 0.01-24";
    }
  });

  registerCommand("REPEATER", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.equalsIgnoreCase("ON") || args == "1") {
      data.SetRepeater(true);
      lora.lastMsgReceived = "SYS: Repeater ON";
    } else if (args.equalsIgnoreCase("OFF") || args == "0") {
      data.SetRepeater(false);
      lora.lastMsgReceived = "SYS: Repeater OFF";
    }
  });

  registerCommand("SETWIFI", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    int sep = args.indexOf(' ');
    if (sep > 0) {
      String ssid = args.substring(0, sep);
      String pass = args.substring(sep + 1);
      pass.trim();
      data.SetWifi(ssid, pass);
      lora.lastMsgReceived = "SYS: WiFi set -> " + ssid;
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
    } else {
      lora.lastMsgReceived = "ERR: SETWIFI ssid pass";
    }
  });

  registerCommand("CONFIG", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();

    String argsCopy = args;
    argsCopy.trim();

    int space = argsCopy.indexOf(' ');
    if (space <= 0) {
      lora.lastMsgReceived = "ERR: CONFIG SET|GET key [value]";
      return;
    }

    String operation = argsCopy.substring(0, space);
    operation.toUpperCase();
    String remainder = argsCopy.substring(space + 1);
    remainder.trim();

    if (operation == "SET") {
      int sep = remainder.indexOf(' ');
      if (sep <= 0) {
        lora.lastMsgReceived = "ERR: CONFIG SET key value";
        return;
      }
      String key = remainder.substring(0, sep);
      String value = remainder.substring(sep + 1);
      key.toUpperCase();
      value.trim();

      // Handle various config keys
      if (key == "DEV_NAME" || key == "DEVICE_NAME") {
        data.SetName(value);
        lora.lastMsgReceived = "CONFIG: dev_name -> " + value;
      } else if (key == "WIFI_SSID") {
        data.SetWifi(value, data.wifiPass);
        lora.lastMsgReceived = "CONFIG: wifi_ssid -> " + value;
      } else if (key == "WIFI_PASS") {
        data.SetWifi(data.wifiSsid, value);
        lora.lastMsgReceived = "CONFIG: wifi_pass -> ****";
      } else if (key == "WIFI_EN") {
        bool enable = value.equalsIgnoreCase("1") || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on");
        data.SetWifiEnabled(enable);
        lora.lastMsgReceived = "CONFIG: wifi_en -> " + String(enable ? "1" : "0");
      } else if (key == "STATIC_IP") {
        data.SetStaticIp(value, "", "");
        lora.lastMsgReceived = "CONFIG: static_ip -> " + value;
      } else if (key == "REPEATER") {
        bool enable = value.equalsIgnoreCase("1") || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on");
        data.SetRepeater(enable);
        lora.lastMsgReceived = "CONFIG: repeater -> " + String(enable ? "1" : "0");
      } else {
        lora.lastMsgReceived = "ERR: Unknown config key: " + key;
        return;
      }

      // Trigger restart for WiFi/IP changes
      if (key == "WIFI_SSID" || key == "WIFI_PASS" || key == "WIFI_EN" || key == "STATIC_IP") {
#ifndef UNIT_TEST
        ScheduleManager::getInstance().triggerRestart(1000);
#endif
      }

    } else if (operation == "GET") {
      String key = remainder;
      key.toUpperCase();

      if (key == "DEV_NAME" || key == "DEVICE_NAME") {
        lora.lastMsgReceived = "CONFIG: dev_name = " + data.myId;
      } else if (key == "WIFI_SSID") {
        lora.lastMsgReceived = "CONFIG: wifi_ssid = " + data.wifiSsid;
      } else if (key == "WIFI_EN") {
        lora.lastMsgReceived = "CONFIG: wifi_en = " + String(data.wifiEnabled ? "1" : "0");
      } else if (key == "STATIC_IP") {
        lora.lastMsgReceived = "CONFIG: static_ip = " + data.staticIp;
      } else if (key == "REPEATER") {
        lora.lastMsgReceived = "CONFIG: repeater = " + String(data.repeaterEnabled ? "1" : "0");
      } else {
        lora.lastMsgReceived = "ERR: Unknown config key: " + key;
      }
    } else {
      lora.lastMsgReceived = "ERR: CONFIG SET|GET";
    }
  });

  registerCommand("WIPECONFIG", [](const String &args, CommInterface source) {
    DataManager::getInstance().FactoryReset();
    LoRaManager::getInstance().lastMsgReceived = "SYS: CONFIG WIPED";
    ScheduleManager::getInstance().triggerRestart(2000);
  });

  registerCommand("SETIP", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    if (args.equalsIgnoreCase("OFF") || args == "0") {
      data.SetStaticIp("", "", "");
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      LOG_PRINTLN("ERR: SETIP rejected. Must be connected to WiFi first.");
      return;
    }
    String gw = WiFi.gatewayIP().toString();
    String sn = WiFi.subnetMask().toString();
    if (args.length() > 0) {
      data.SetStaticIp(args, gw, sn);
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
    }
  });

  registerCommand("ESPNOW", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.equalsIgnoreCase("ON") || args == "1") {
      data.SetESPNowEnabled(true);
      lora.lastMsgReceived = "SYS: ESP-NOW ON (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    } else if (args.equalsIgnoreCase("OFF") || args == "0") {
      data.SetESPNowEnabled(false);
      lora.lastMsgReceived = "SYS: ESP-NOW OFF (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    }
  });

  registerCommand("RADIO", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (args.equalsIgnoreCase("MODE LORA_BLE_WIFI")) {
      data.SetWifiEnabled(true);
      data.SetBleEnabled(true);
      lora.lastMsgReceived = "SYS: RADIO ALL ON (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    } else if (args.equalsIgnoreCase("MODE LORA_BLE")) {
      data.SetWifiEnabled(false);
      data.SetBleEnabled(true);
      lora.lastMsgReceived = "SYS: RADIO LORA+BLE (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    } else if (args.equalsIgnoreCase("MODE LORA_WIFI")) {
      data.SetWifiEnabled(true);
      data.SetBleEnabled(false);
      lora.lastMsgReceived = "SYS: RADIO LORA+WIFI (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    } else if (args.equalsIgnoreCase("MODE LORA_ONLY")) {
      data.SetWifiEnabled(false);
      data.SetBleEnabled(false);
      lora.lastMsgReceived = "SYS: RADIO LORA ONLY (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    }
  });

  registerCommand("SETKEY", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    String key = args;
    key.trim();
    if (key.length() != 32) {
      lora.lastMsgReceived = "ERR: Key must be 32 hex chars";
      LOG_PRINTLN("SETKEY: Invalid length: " + String(key.length()));
      return;
    }
    uint8_t testBuf[16];
    if (!parseHexKey(key.c_str(), testBuf)) {
      lora.lastMsgReceived = "ERR: Invalid hex characters";
      LOG_PRINTLN("SETKEY: Bad hex");
      return;
    }
    data.SetCryptoKey(key);
    lora.lastMsgReceived = "SYS: AES Key Updated (Reboot)";
    LOG_PRINTLN("SETKEY: Key saved to NVS");
    ScheduleManager::getInstance().triggerRestart(1000);
  });

  registerCommand("NODES", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    if (data.numNodes == 0) {
      lora.lastMsgReceived = "MESH: No nodes discovered";
      LOG_PRINTLN("MESH: No nodes discovered");
      return;
    }
    unsigned long now = millis();
    String result = "MESH: " + String(data.numNodes) + " nodes\n";
    for (int i = 0; i < data.numNodes; i++) {
      result += "[" + String(data.remoteNodes[i].id) + "] ";
      result += "bat=" + String(data.remoteNodes[i].battery, 2) + "V ";
      result += "rssi=" + String(data.remoteNodes[i].rssi) + " ";
      result += "hops=" + String(data.remoteNodes[i].hops) + " ";
      result += "seen=" + String((now - data.remoteNodes[i].lastSeen) / 1000) +
                "s ago\n";
    }
    lora.lastMsgReceived = result;
    LOG_PRINT(result);
  });

  registerCommand("INJECT", [this](const String &args, CommInterface source) {
    LOG_PRINTLN("DBG: Injecting LoRa Packet: " + args);
    handleCommand(args, CommInterface::COMM_LORA);
  });

  registerCommand("GPIO", [this](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    int space = args.indexOf(' ');
    if (space > 0) {
      String pinName = args.substring(0, space);
      int pin = getPinFromName(pinName);
      int val = args.substring(space + 1).toInt();
      if (pin >= 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val);
        data.SetGpioState(pinName, val == 1);
        String friendly = data.GetPinName(String(pin));
        String msg = "SYS: GPIO " + pinName + "=" + String(val);
        if (friendly.length() > 0)
          msg += " (" + friendly + ")";
        lora.lastMsgReceived = msg;
        if (source == CommInterface::COMM_LORA)
          lora.SendLoRa(data.myId + " " + msg);
        else
          LOG_PRINTLN(msg);
      }
    }
  });

  registerCommand("READ", [this](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    String pinName = args;
    pinName.trim();
    int pin = getPinFromName(pinName);
    if (pin >= 0) {
      pinMode(pin, INPUT);
      int val = digitalRead(pin);
      String friendly = data.GetPinName(String(pin));
      String msg = "SYS: READ " + pinName + "=" + String(val);
      if (friendly.length() > 0)
        msg += " (" + friendly + ")";
      lora.lastMsgReceived = msg;
      if (source == CommInterface::COMM_LORA)
        lora.SendLoRa(data.myId + " " + msg);
      else
        LOG_PRINTLN(msg);
    }
  });

  registerCommand("PINNAME", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    int space = args.indexOf(' ');
    if (space > 0) {
      String pinStr = args.substring(0, space);
      String name = args.substring(space + 1);
      name.trim();
      data.SetPinName(pinStr, name);
      String msg = "SYS: Pin " + pinStr + " renamed to " + name;
      lora.lastMsgReceived = msg;
      LOG_PRINTLN(msg);
    } else {
      lora.lastMsgReceived = "ERR: PINNAME pin friendly_name";
    }
  });

  registerCommand("SCHED", [](const String &args, CommInterface source) {
    LoRaManager &lora = LoRaManager::getInstance();
    ScheduleManager &sched = ScheduleManager::getInstance();

    int splitIdx = args.indexOf(' ');
    int nlIdx = args.indexOf('\n');
    if (splitIdx == -1 || (nlIdx != -1 && nlIdx < splitIdx)) {
      splitIdx = nlIdx;
    }

    String sub = (splitIdx == -1) ? args : args.substring(0, splitIdx);
    String subArgs = (splitIdx == -1) ? "" : args.substring(splitIdx + 1);
    sub.trim();
    sub.toUpperCase();

    if (sub == "LIST") {
      String report = sched.getTaskReport();
      if (source == CommInterface::COMM_LORA)
        lora.SendLoRa(report);
      else
        LOG_PRINT(report);
    } else if (sub == "ADD") {
      // ADD name type pin interval duration
      String parts[5];
      int pIdx = 0;
      int pStart = 0;
      int pEnd = subArgs.indexOf(' ');
      while (pIdx < 5) {
        if (pEnd == -1)
          pEnd = subArgs.length();
        parts[pIdx++] = subArgs.substring(pStart, pEnd);
        if (pEnd == (int)subArgs.length())
          break;
        pStart = pEnd + 1;
        pEnd = subArgs.indexOf(' ', pStart);
      }

      if (pIdx >= 4) {
        if (sched.addDynamicTask(parts[0], parts[1], parts[2], parts[3].toInt(),
                                 (pIdx > 4) ? parts[4].toInt() : 0,
                                 interfaceName(source))) {
          lora.lastMsgReceived = "SCHED: Added " + parts[0];
        } else {
          lora.lastMsgReceived = "ERR: Sched pool full";
        }
      } else {
        lora.lastMsgReceived =
            "ERR: SCHED ADD name type pin interval(s) [dur(s)]";
      }
    } else if (sub == "IMPORT") {
      sched.setStreamMode(true);
      LOG_PRINTLN("STREAM: Ready for Excel Data Streamer...");
      LOG_PRINTLN("STREAM: Send rows (Name,Type,Pin,Interval,Duration)");
      LOG_PRINTLN("STREAM: Send 'END' to finish.");
    } else if (sub == "CLEAR") {
      sched.clearDynamicTasks();
      lora.lastMsgReceived = "SCHED: All dynamic tasks cleared";
    } else if (sub == "REM") {
      if (sched.removeDynamicTask(subArgs)) {
        lora.lastMsgReceived = "SCHED: Removed " + subArgs;
      } else {
        lora.lastMsgReceived = "ERR: Task not found";
      }
    } else if (sub == "SAVE") {
      sched.saveDynamicTasks();
      lora.lastMsgReceived = "SCHED: Pool saved to FS";
    } else {
      // Legacy support for SETSCHED format (using seconds)
      unsigned long s = args.toInt();
      if (s > 0) {
        unsigned long ms = s * 1000;
        DataManager::getInstance().SetSchedulerInterval(ms);
        sched.set110VInterval(ms);
        lora.lastMsgReceived = "SYS: SCHED " + String(s) + "s";
      } else {
        lora.lastMsgReceived = "HELP: SCHED LIST|ADD|REM|SAVE|IMPORT";
      }
    }
  });

  registerCommand("RELAY", [](const String &args, CommInterface source) {
    if (args.equalsIgnoreCase("110V ON")) {
      ScheduleManager::getInstance().forceRelay110V(true);
    } else if (args.equalsIgnoreCase("110V OFF")) {
      ScheduleManager::getInstance().forceRelay110V(false);
    }
  });

  registerCommand("NEXTPAGE", [](const String &args, CommInterface source) {
    LoRaManager &lora = LoRaManager::getInstance();
    DisplayManager::getInstance().NextPage();
    lora.lastMsgReceived = "SYS: PAGE CHANGED";
  });

  registerCommand("APC", [this](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    int space = args.indexOf(' ');
    if (space > 0) {
      String pinName = args.substring(0, space);
      int pin = getPinFromName(pinName);
      int en = args.substring(space + 1).toInt();
      if (pin >= 0) {
        data.SetPinEnabled(pin, en == 1);
        String msg = "APC: Pin " + pinName + (en ? " ENABLED" : " DISABLED");
        lora.lastMsgReceived = msg;
        LOG_PRINTLN(msg);
      }
    }
  });

  registerCommand("TRANS", [](const String &args, CommInterface source) {
    DataManager &data = DataManager::getInstance();
    LoRaManager &lora = LoRaManager::getInstance();
    String mode = args;
    mode.trim();
    mode.toUpperCase();
    if (mode.length() == 1) {
      char m = mode.charAt(0);
      if (m == 'J' || m == 'C' || m == 'K' || m == 'B') {
        data.SetTransportMode(m);
        String msg = "TRANS: Mode set to " + mode;
        lora.lastMsgReceived = msg;
        LOG_PRINTLN(msg);
        return;
      }
    }
    lora.lastMsgReceived = "ERR: TRANS J|C|K|B";
  });
}

void CommandManager::executeLocalCommand(const String &cmd,
                                         CommInterface source) {
  LOG_PRINTF("CMD: Executing [%s] (from %s)\n", cmd.c_str(),
             interfaceName(source));

  String cmdCopy = cmd;
  cmdCopy.trim();

  int space = cmdCopy.indexOf(' ');
  String verb = cmdCopy;
  String args = "";
  if (space > 0) {
    verb = cmdCopy.substring(0, space);
    args = cmdCopy.substring(space + 1);
    args.trim();
  }
  verb.toUpperCase();

  if (_commandRegistry.count(verb)) {
    _commandRegistry[verb](args, source);
  } else {
    LoRaManager::getInstance().lastMsgReceived = "ERR: Unknown Local Cmd";
  }
}
