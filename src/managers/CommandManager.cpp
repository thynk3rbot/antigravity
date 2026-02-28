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
  bool fromRemote = (source != CommInterface::COMM_SERIAL &&
                     source != CommInterface::COMM_INTERNAL);

  DataManager &data = DataManager::getInstance();
  DisplayManager &display = DisplayManager::getInstance();
  LoRaManager &lora = LoRaManager::getInstance();

  display.SetDisplayActive(true);

  LOG_PRINTF("CMD: [%s] via %s\n", fullCmd.c_str(), interfaceName(source));

  // "CMD:" prefix legacy support
  if (fullCmd.startsWith("CMD:")) {
    fullCmd = fullCmd.substring(4);
    fullCmd.trim();
  }

  // --- GLOBAL COMMANDS ---

  // SETNAME
  if (strncasecmp(fullCmd.c_str(), "SETNAME", 7) == 0) {
    String newName = fullCmd.substring(7);
    newName.trim();
    if (newName.length() > 0 && newName.length() < 15) {
      data.SetName(newName);
      lora.lastMsgReceived = "SYS: Named " + newName;
      LOG_PRINTLN("SETNAME -> " + newName);
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
    } else {
      lora.lastMsgReceived = "ERR: Name 1-14 chars";
    }
    return;
  }

  // SLEEP
  if (strncasecmp(fullCmd.c_str(), "SLEEP", 5) == 0) {
    String arg = fullCmd.substring(5);
    arg.trim();
    float hours = arg.toFloat();
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
    return;
  }

  // REPEATER
  if (strncasecmp(fullCmd.c_str(), "REPEATER", 8) == 0) {
    String arg = fullCmd.substring(8);
    arg.trim();
    if (arg.equalsIgnoreCase("ON") || arg == "1") {
      data.SetRepeater(true);
      lora.lastMsgReceived = "SYS: Repeater ON";
    } else if (arg.equalsIgnoreCase("OFF") || arg == "0") {
      data.SetRepeater(false);
      lora.lastMsgReceived = "SYS: Repeater OFF";
    }
    return;
  }

  // SETWIFI
  if (strncasecmp(fullCmd.c_str(), "SETWIFI", 7) == 0) {
    String args = fullCmd.substring(7);
    args.trim();
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
    return;
  }

  // WIPECONFIG
  if (strncasecmp(fullCmd.c_str(), "WIPECONFIG", 10) == 0) {
    data.FactoryReset();
    lora.lastMsgReceived = "SYS: CONFIG WIPED";
    ScheduleManager::getInstance().triggerRestart(2000);
    return;
  }

  // SETIP
  if (strncasecmp(fullCmd.c_str(), "SETIP", 5) == 0) {
    String args = fullCmd.substring(5);
    args.trim();
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
    String ip = args;
    String gw = WiFi.gatewayIP().toString();
    String sn = WiFi.subnetMask().toString();
    if (ip.length() > 0) {
      data.SetStaticIp(ip, gw, sn);
#ifndef UNIT_TEST
      ScheduleManager::getInstance().triggerRestart(1000);
#endif
    }
    return;
  }

  // ESPNOW
  if (strncasecmp(fullCmd.c_str(), "ESPNOW", 6) == 0) {
    String arg = fullCmd.substring(6);
    arg.trim();
    if (arg.equalsIgnoreCase("ON") || arg == "1") {
      data.SetESPNowEnabled(true);
      lora.lastMsgReceived = "SYS: ESP-NOW ON (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    } else if (arg.equalsIgnoreCase("OFF") || arg == "0") {
      data.SetESPNowEnabled(false);
      lora.lastMsgReceived = "SYS: ESP-NOW OFF (Reboot)";
      ScheduleManager::getInstance().triggerRestart(1000);
    }
    return;
  }

  // INJECT (Test Remote Logic)
  if (strncasecmp(fullCmd.c_str(), "INJECT", 6) == 0) {
    String payload = fullCmd.substring(6);
    payload.trim();
    LOG_PRINTLN("DBG: Injecting LoRa Packet: " + payload);
    handleCommand(payload, CommInterface::COMM_LORA);
    return;
  }

  // --- TARGETED COMMANDS ---
  int space = fullCmd.indexOf(' ');
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
        lora.SendLoRa(fullCmd);
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
      }
    } else {
      // Not for us - forward to network
      if (!fromRemote) {
        lora.SendLoRa(fullCmd);
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
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
        lora.SendLoRa(fullCmd);
        if (ESPNowManager::getInstance().espNowActive) {
          ESPNowManager::getInstance().sendToAll(fullCmd);
        }
      }
    }
  }
}

void CommandManager::executeLocalCommand(const String &cmd,
                                         CommInterface source) {
  LOG_PRINTF("CMD: Executing [%s] (from %s)\n", cmd.c_str(),
             interfaceName(source));

  bool fromLoRa = (source == CommInterface::COMM_LORA);
  String cmdCopy = cmd;
  cmdCopy.trim();

  DataManager &data = DataManager::getInstance();
  LoRaManager &lora = LoRaManager::getInstance();

  if (cmdCopy.equalsIgnoreCase("LED ON")) {
    digitalWrite(PIN_LED_BUILTIN, HIGH);
    data.SetGpioState("LED", true);
    lora.lastMsgReceived = "SYS: LED ON";
  } else if (cmdCopy.equalsIgnoreCase("LED OFF")) {
    digitalWrite(PIN_LED_BUILTIN, LOW);
    data.SetGpioState("LED", false);
    lora.lastMsgReceived = "SYS: LED OFF";
  } else if (cmdCopy.equalsIgnoreCase("STATUS")) {
    float bat = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                : "DISCONNECTED";
    String msg = "ID: " + data.myId + " (HW: [" + data.getMacSuffix() + "]) ";
    msg += "IP: " + ip + " ";
    msg += "BAT: " + String(bat, 2) + "V ";
    msg += "LoRa: " + String(lora.lastRssi) + "dBm";
    msg += " EN:" +
           String(ESPNowManager::getInstance().espNowActive ? "ON" : "OFF");
    if (fromLoRa) {
      lora.SendLoRa(data.myId + " " + msg);
    } else {
      LOG_PRINTLN(msg);
    }
    lora.lastMsgReceived = "SYS: STATUS SENT";
  } else if (cmdCopy.equalsIgnoreCase("BLINK")) {
    ScheduleManager::getInstance().triggerBlink();
    lora.lastMsgReceived = "SYS: BLINK";
  } else if (cmdCopy.equalsIgnoreCase("READMAC")) {
    String mac = WiFi.macAddress();
    if (fromLoRa)
      lora.SendLoRa("MAC: " + mac);
    else
      LOG_PRINTLN("MAC: " + mac);
    lora.lastMsgReceived = "SYS: MAC SENT";
  } else if (cmdCopy.equalsIgnoreCase("RADIO")) {
    lora.DumpDiagnostics();
    lora.lastMsgReceived = "SYS: RADIO DIAG SENT";
  } else if (cmdCopy.equalsIgnoreCase("HELP")) {
    String help = "LED ON/OFF, BLINK, STATUS, READMAC, RADIO, SETNAME, "
                  "SETWIFI, ESPNOW ON/OFF";
    if (fromLoRa)
      lora.SendLoRa(help);
    else
      LOG_PRINTLN(help);
  } else if (strncasecmp(cmdCopy.c_str(), "GPIO", 4) == 0) {
    int firstSpace = cmdCopy.indexOf(' ');
    int secondSpace =
        (firstSpace > 0) ? cmdCopy.indexOf(' ', firstSpace + 1) : -1;
    if (firstSpace > 0 && secondSpace > 0) {
      String pinName = cmdCopy.substring(firstSpace + 1, secondSpace);
      int pin = getPinFromName(pinName);
      int val = cmdCopy.substring(secondSpace + 1).toInt();
      if (pin >= 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val);
        data.SetGpioState(pinName, val == 1);
        String msg = "SYS: GPIO " + pinName + "=" + String(val);
        lora.lastMsgReceived = msg;
        if (fromLoRa)
          lora.SendLoRa(data.myId + " " + msg);
        else
          LOG_PRINTLN(msg);
      }
    }
  } else if (strncasecmp(cmdCopy.c_str(), "READ", 4) == 0) {
    int sp = cmdCopy.indexOf(' ');
    if (sp > 0) {
      String pinName = cmdCopy.substring(sp + 1);
      int pin = getPinFromName(pinName);
      if (pin >= 0) {
        pinMode(pin, INPUT);
        int val = digitalRead(pin);
        String msg = "SYS: READ " + pinName + "=" + String(val);
        lora.lastMsgReceived = msg;
        if (fromLoRa)
          lora.SendLoRa(data.myId + " " + msg);
        else
          LOG_PRINTLN(msg);
      }
    }
  } else if (cmdCopy.equalsIgnoreCase("GETSCHED")) {
    String json = DataManager::getInstance().ReadSchedule();
    Serial.println("SCHED_JSON:" + json);
    lora.lastMsgReceived = "SYS: SCHED SENT TO SERIAL";
  } else if (strncasecmp(cmdCopy.c_str(), "SETSCHED ", 9) == 0) {
    String payload = cmdCopy.substring(9);
    payload.trim();
    if (payload.startsWith("{")) {
      if (DataManager::getInstance().SaveSchedule(payload)) {
        ScheduleManager::getInstance().loadDynamicSchedules();
        lora.lastMsgReceived = "SYS: JSON SCHED UPDATED";
      }
    } else if (payload.indexOf(',') > 0) {
      ScheduleManager::getInstance().loadSchedulesFromCsv(payload);
      lora.lastMsgReceived = "SYS: CSV SCHED UPDATED";
    } else {
      unsigned long ms = payload.toInt();
      if (ms > 0) {
        DataManager::getInstance().SetSchedulerInterval(ms);
        ScheduleManager::getInstance().set110VInterval(ms);
        lora.lastMsgReceived = "SYS: SCHED " + String(ms) + "ms";
      }
    }
  } else if (cmdCopy.startsWith("RELAY ")) {
    if (cmdCopy.substring(6) == "110V ON") {
      ScheduleManager::getInstance().forceRelay110V(true);
    } else if (cmdCopy.substring(6) == "110V OFF") {
      ScheduleManager::getInstance().forceRelay110V(false);
    }
  } else if (cmdCopy.equalsIgnoreCase("NEXTPAGE")) {
    DisplayManager::getInstance().NextPage();
    lora.lastMsgReceived = "SYS: PAGE CHANGED";
  } else {
    lora.lastMsgReceived = "ERR: Unknown Local Cmd";
  }
}
