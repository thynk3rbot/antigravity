#include "WiFiManager.h"
#include "../utils/DebugMacros.h"
#include "ESPNowManager.h"
#include "LoRaManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>

WebServer server(80);

void (*_webCmdCallback)(const String &, CommInterface) = NULL;
void setWebCallback(void (*cb)(const String &, CommInterface)) {
  _webCmdCallback = cb;
}

WiFiManager::WiFiManager() {
  isConnected = false;
  serverStarted = false;
  lastWifiTry = 0;
}

void WiFiManager::init() {
  DataManager &data = DataManager::getInstance();
  if (data.wifiSsid.length() > 0) {
    Serial.println("WiFi: Configured (Staggered Start)...");
    WiFi.mode(WIFI_STA);
  } else {
    Serial.println("WiFi: No SSID saved.");
  }
}

void WiFiManager::handle() {
  DataManager &data = DataManager::getInstance();

  if (millis() < 5000)
    return;

  if (data.wifiSsid.length() > 0) {
    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiTry > 10000) {
      static bool configured = false;
      if (!configured) {
        if (data.staticIp.length() > 0) {
          IPAddress ip, gw, sn;
          if (ip.fromString(data.staticIp) && gw.fromString(data.gateway) &&
              sn.fromString(data.subnet)) {
            WiFi.config(ip, gw, sn);
          }
        }
        WiFi.begin(data.wifiSsid.c_str(), data.wifiPass.c_str());
        configured = true;
        lastWifiTry = millis();
        Serial.println("WiFi: Attempting connection...");
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      if (!isConnected) {
        isConnected = true;
        Serial.println("WiFi: Connected! IP: " + WiFi.localIP().toString());
      }
      if (!serverStarted) {
        startServer();
        ArduinoOTA.begin();
        serverStarted = true;
        Serial.println("Web server & OTA started");
      }
      ArduinoOTA.handle();
      server.handleClient();
    } else {
      isConnected = false;
      if (millis() - lastWifiTry > 10000) {
        lastWifiTry = millis();
        WiFi.disconnect();
        WiFi.begin(data.wifiSsid.c_str(), data.wifiPass.c_str());
      }
    }
  }
}

void WiFiManager::startServer() {
  DataManager &data = DataManager::getInstance();
  String otaName = "LoRaLink-" + data.myId;
  ArduinoOTA.setHostname(otaName.c_str());
  ArduinoOTA.onStart([]() {
    DisplayManager::getInstance().SetDisplayActive(true);
    Heltec.display->clear();
    Heltec.display->drawString(0, 20, "OTA Updating...");
    Heltec.display->display();
  });

  // Dashboard
  server.on("/", HTTP_GET, [this]() { serveHome(); });

  // Configuration page
  server.on("/config", HTTP_GET, [this]() { serveConfig(); });
  server.on("/config", HTTP_POST, [this]() { serveConfigSave(); });

  // API
  server.on("/api/status", HTTP_GET, [this]() { serveApiStatus(); });
  server.on("/api/cmd", HTTP_POST, [this]() { serveApiCmd(); });
  server.on("/api/peers", HTTP_GET, [this]() { serveApiPeers(); });
  server.on("/api/peers/add", HTTP_POST, [this]() { serveApiAddPeer(); });
  server.on("/api/peers/remove", HTTP_POST, [this]() { serveApiRemovePeer(); });

  server.begin();
}

// ============================================================================
//   DASHBOARD PAGE
// ============================================================================
void WiFiManager::serveHome() {
  String html = R"rawhtml(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LoRaLink Dashboard</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f0f1a;color:#e0e0e0;min-height:100vh}
.hdr{background:linear-gradient(135deg,#1a1a2e,#16213e);padding:16px 20px;border-bottom:1px solid #2a2a4a;display:flex;justify-content:space-between;align-items:center}
.hdr h1{font-size:1.3em;color:#00d4ff;font-weight:600}
.hdr a{color:#888;text-decoration:none;font-size:0.85em;padding:6px 14px;border:1px solid #2a2a4a;border-radius:6px}
.hdr a:hover{color:#00d4ff;border-color:#00d4ff}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;padding:16px}
.card{background:#1a1a2e;border-radius:10px;padding:14px;border:1px solid #2a2a4a}
.card .lbl{font-size:0.7em;color:#888;text-transform:uppercase;letter-spacing:1px}
.card .val{font-size:1.4em;font-weight:700;margin-top:4px}
.card .val.ok{color:#00ff88}
.card .val.warn{color:#ffaa00}
.log{margin:16px;background:#1a1a2e;border-radius:10px;border:1px solid #2a2a4a;max-height:240px;overflow-y:auto}
.log .m{padding:6px 16px;font-size:0.85em;border-bottom:1px solid #1f1f3a;font-family:monospace}
.cmd{display:flex;gap:8px;padding:16px}
.cmd input{flex:1;background:#1a1a2e;border:1px solid #2a2a4a;border-radius:8px;padding:10px 14px;color:#fff;outline:none}
.cmd input:focus{border-color:#00d4ff}
.cmd button{background:#00d4ff;border:none;border-radius:8px;padding:10px 20px;color:#0f0f1a;cursor:pointer;font-weight:600}
.cmd button:hover{background:#00b8d4}
.ifc{display:flex;gap:6px;padding:0 16px;flex-wrap:wrap}
.ifc .badge{padding:4px 10px;border-radius:12px;font-size:0.7em;font-weight:600;letter-spacing:1px}
.ifc .on{background:#00ff8822;color:#00ff88;border:1px solid #00ff8844}
.ifc .off{background:#ff444422;color:#ff4444;border:1px solid #ff444444}
</style></head><body>
<div class='hdr'><h1>&#x1F4E1; LoRaLink AnyToAny</h1><a href='/config'>&#x2699; Config</a></div>
<div class='ifc' id='ifc'></div>
<div class='grid' id='cards'></div>
<div class='log'><div class='m' style='color:#00d4ff'>Message Log</div><div id='log'></div></div>
<div class='cmd'><input id='ci' placeholder='Command (e.g. ALL LED ON)' onkeydown="if(event.key==='Enter')send()"><button onclick='send()'>Send</button></div>
<script>
function up(){fetch('/api/status').then(r=>r.json()).then(d=>{
document.getElementById('ifc').innerHTML=
`<span class="badge ${d.lora?'on':'off'}">LoRa</span>`+
`<span class="badge ${d.ble?'on':'off'}">BLE</span>`+
`<span class="badge ${d.wifi?'on':'off'}">WiFi</span>`+
`<span class="badge ${d.espnow?'on':'off'}">ESP-NOW</span>`;
let c=`<div class="card"><div class="lbl">Device</div><div class="val">${d.id}</div></div>`;
c+=`<div class="card"><div class="lbl">Uptime</div><div class="val">${d.uptime}</div></div>`;
c+=`<div class="card"><div class="lbl">Battery</div><div class="val ${d.bat>3.5?'ok':'warn'}">${d.bat}V</div></div>`;
c+=`<div class="card"><div class="lbl">LoRa RSSI</div><div class="val">${d.rssi} dBm</div></div>`;
c+=`<div class="card"><div class="lbl">Nodes</div><div class="val">${d.nodes}</div></div>`;
c+=`<div class="card"><div class="lbl">Heap</div><div class="val">${d.heap}</div></div>`;
document.getElementById('cards').innerHTML=c;
let l='';d.log.forEach(m=>{if(m)l+=`<div class="m">${m}</div>`;});
document.getElementById('log').innerHTML=l;
})}
setInterval(up,3000);up();
function send(){let v=document.getElementById('ci').value;if(!v)return;
fetch('/api/cmd',{method:'POST',body:new URLSearchParams({'cmd':v})});document.getElementById('ci').value='';}
</script></body></html>)rawhtml";
  server.send(200, "text/html", html);
}

// ============================================================================
//   CONFIGURATION PAGE
// ============================================================================
void WiFiManager::serveConfig() {
  DataManager &data = DataManager::getInstance();
  ESPNowManager &espnow = ESPNowManager::getInstance();

  String html = R"rawhtml(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LoRaLink Config</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f0f1a;color:#e0e0e0;min-height:100vh}
.hdr{background:linear-gradient(135deg,#1a1a2e,#16213e);padding:16px 20px;border-bottom:1px solid #2a2a4a;display:flex;justify-content:space-between;align-items:center}
.hdr h1{font-size:1.3em;color:#00d4ff;font-weight:600}
.hdr a{color:#888;text-decoration:none;font-size:0.85em;padding:6px 14px;border:1px solid #2a2a4a;border-radius:6px}
.hdr a:hover{color:#00d4ff;border-color:#00d4ff}
.sec{margin:16px;background:#1a1a2e;border-radius:10px;border:1px solid #2a2a4a;padding:20px}
.sec h2{color:#00d4ff;font-size:1em;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #2a2a4a}
.row{display:flex;gap:12px;margin-bottom:10px;align-items:center;flex-wrap:wrap}
.row label{width:120px;font-size:0.85em;color:#aaa;flex-shrink:0}
.row input,.row select{flex:1;background:#0f0f1a;border:1px solid #2a2a4a;border-radius:6px;padding:8px 12px;color:#fff;outline:none;min-width:140px}
.row input:focus,.row select:focus{border-color:#00d4ff}
.row .hint{font-size:0.7em;color:#666;width:100%;margin-left:132px}
.btn{background:#00d4ff;border:none;border-radius:8px;padding:10px 24px;color:#0f0f1a;cursor:pointer;font-weight:600;margin-top:12px}
.btn:hover{background:#00b8d4}
.btn.danger{background:#ff4444;color:#fff}
.btn.danger:hover{background:#cc3333}
.peer-row{display:flex;gap:8px;align-items:center;padding:6px 0;border-bottom:1px solid #1f1f3a}
.peer-row .mac{font-family:monospace;font-size:0.85em;color:#aaa}
.peer-row .name{font-weight:600;color:#e0e0e0}
.peer-row button{background:#ff444444;border:1px solid #ff4444;color:#ff4444;border-radius:4px;padding:2px 8px;cursor:pointer;font-size:0.75em}
.peer-row button:hover{background:#ff4444;color:#fff}
.add-peer{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}
.add-peer input{background:#0f0f1a;border:1px solid #2a2a4a;border-radius:6px;padding:8px 12px;color:#fff;outline:none}
.tag{display:inline-block;padding:2px 8px;border-radius:10px;font-size:0.7em;font-weight:600;margin-left:6px}
.tag.on{background:#00ff8822;color:#00ff88}
.tag.off{background:#ff444422;color:#ff4444}
.msg{background:#00ff8822;color:#00ff88;border:1px solid #00ff8844;border-radius:8px;padding:10px 16px;margin:16px;text-align:center;display:none}
</style></head><body>
<div class='hdr'><h1>&#x2699; Configuration</h1><a href='/'>&#x1F4E1; Dashboard</a></div>
<div class='msg' id='msg'></div>
<form method='POST' action='/config'>

<div class='sec'>
<h2>&#x1F4BB; Device</h2>
<div class='row'><label>Device Name</label><input name='dev_name' value=')rawhtml";
  html += data.myId;
  html += R"rawhtml(' maxlength='14'></div>
<div class='row'><label>Repeater</label><select name='repeater'><option value='0')rawhtml";
  if (!data.repeaterEnabled)
    html += " selected";
  html += R"rawhtml(>OFF</option><option value='1')rawhtml";
  if (data.repeaterEnabled)
    html += " selected";
  html += R"rawhtml(>ON</option></select></div>
</div>

<div class='sec'>
<h2>&#x1F4F6; WiFi</h2>
<div class='row'><label>SSID</label><input name='wifi_ssid' value=')rawhtml";
  html += data.wifiSsid;
  html += R"rawhtml('></div>
<div class='row'><label>Password</label><input name='wifi_pass' type='password' value=')rawhtml";
  html += data.wifiPass;
  html += R"rawhtml('></div>
<div class='row'><label>Static IP</label><input name='static_ip' value=')rawhtml";
  html += data.staticIp;
  html += R"rawhtml(' placeholder='Leave blank for DHCP'></div>
<div class='row'><label>Gateway</label><input name='gateway' value=')rawhtml";
  html += data.gateway;
  html += R"rawhtml('></div>
<div class='row'><label>Subnet</label><input name='subnet' value=')rawhtml";
  html += data.subnet;
  html += R"rawhtml('></div>
</div>

<div class='sec'>
<h2>&#x1F4E1; LoRa Radio</h2>
<div class='row'><label>Frequency</label><input value=')rawhtml";
  html += String(LORA_FREQ, 1);
  html +=
      R"rawhtml(' disabled><div class='hint'>Compile-time: change in config.h</div></div>
<div class='row'><label>Bandwidth</label><input value=')rawhtml";
  html += String(LORA_BW, 1);
  html += R"rawhtml(' disabled></div>
<div class='row'><label>Spread Factor</label><input value=')rawhtml";
  html += String(LORA_SF);
  html += R"rawhtml(' disabled></div>
<div class='row'><label>TX Power</label><input value=')rawhtml";
  html += String(LORA_PWR);
  html += R"rawhtml( dBm' disabled></div>
</div>

<div class='sec'>
<h2>&#x26A1; ESP-NOW</h2>
<div class='row'><label>Enabled</label><select name='espnow_en'><option value='0')rawhtml";
  if (!data.espNowEnabled)
    html += " selected";
  html += R"rawhtml(>OFF</option><option value='1')rawhtml";
  if (data.espNowEnabled)
    html += " selected";
  html += R"rawhtml(>ON</option></select></div>
<div class='row'><label>Channel</label><input name='espnow_ch' type='number' min='1' max='14' value=')rawhtml";
  html += String(data.espNowChannel);
  html += R"rawhtml('></div>
<h2 style='margin-top:16px'>Peers</h2>
<div id='peers'>Loading...</div>
<div class='add-peer'>
<input id='pmac' placeholder='AA:BB:CC:DD:EE:FF' style='width:180px'>
<input id='pname' placeholder='Name' style='width:120px'>
<button type='button' class='btn' onclick='addPeer()' style='margin-top:0'>Add Peer</button>
</div>
</div>

<div class='sec'>
<h2>&#x1F527; Actions</h2>
<button type='submit' class='btn'>&#x1F4BE; Save & Reboot</button>
<button type='button' class='btn danger' onclick="if(confirm('Factory Reset?'))fetch('/api/cmd',{method:'POST',body:new URLSearchParams({cmd:'WIPECONFIG'})})">&#x26A0; Factory Reset</button>
</div>

</form>
<script>
function loadPeers(){fetch('/api/peers').then(r=>r.json()).then(d=>{
let h='';d.forEach((p,i)=>{
h+=`<div class='peer-row'><span class='name'>${p.name}</span><span class='mac'>${p.mac}</span><button onclick="removePeer('${p.mac}')">Remove</button></div>`;
});
if(!h)h='<div style="color:#666;font-size:0.85em">No peers configured</div>';
document.getElementById('peers').innerHTML=h;
})}
loadPeers();
function addPeer(){
let mac=document.getElementById('pmac').value;
let name=document.getElementById('pname').value;
if(!mac||!name)return;
fetch('/api/peers/add',{method:'POST',body:new URLSearchParams({mac:mac,name:name})}).then(()=>{
document.getElementById('pmac').value='';document.getElementById('pname').value='';loadPeers();});
}
function removePeer(mac){
fetch('/api/peers/remove',{method:'POST',body:new URLSearchParams({mac:mac})}).then(()=>loadPeers());
}
</script></body></html>)rawhtml";

  server.send(200, "text/html", html);
}

// ============================================================================
//   CONFIG SAVE HANDLER
// ============================================================================
void WiFiManager::serveConfigSave() {
  DataManager &data = DataManager::getInstance();

  if (server.hasArg("dev_name")) {
    String name = server.arg("dev_name");
    name.trim();
    if (name.length() > 0 && name.length() < 15) {
      data.SetName(name);
    }
  }

  if (server.hasArg("repeater")) {
    data.SetRepeater(server.arg("repeater") == "1");
  }

  if (server.hasArg("wifi_ssid")) {
    data.SetWifi(server.arg("wifi_ssid"), server.arg("wifi_pass"));
  }

  if (server.hasArg("static_ip")) {
    data.SetStaticIp(server.arg("static_ip"), server.arg("gateway"),
                     server.arg("subnet"));
  }

  if (server.hasArg("espnow_en")) {
    data.SetESPNowEnabled(server.arg("espnow_en") == "1");
  }

  if (server.hasArg("espnow_ch")) {
    Preferences p;
    p.begin("loralink", false);
    p.putUChar("espnow_ch", server.arg("espnow_ch").toInt());
    p.end();
  }

  // Respond with redirect, then reboot
  server.send(
      200, "text/html",
      "<html><body "
      "style='background:#0f0f1a;color:#00ff88;font-family:sans-serif;"
      "display:flex;align-items:center;justify-content:center;height:100vh'>"
      "<div style='text-align:center'><h2>&#x2705; Settings Saved</h2>"
      "<p>Rebooting in 3 seconds...</p></div></body></html>");

  delay(3000);
  ESP.restart();
}

// ============================================================================
//   API ENDPOINTS
// ============================================================================
void WiFiManager::serveApiStatus() {
  DataManager &data = DataManager::getInstance();
  LoRaManager &lora = LoRaManager::getInstance();
  ESPNowManager &espnow = ESPNowManager::getInstance();

  float bat = analogRead(PIN_BAT_ADC) / 4095.0 * 3.3 * 2.0;
  unsigned long s = millis() / 1000;
  String uptime = String(s / 3600) + "h " + String((s % 3600) / 60) + "m";

  String json = "{";
  json += "\"id\":\"" + data.myId + "\",";
  json += "\"uptime\":\"" + uptime + "\",";
  json += "\"bat\":" + String(bat, 2) + ",";
  json += "\"rssi\":" + String(lora.lastRssi) + ",";
  json += "\"nodes\":" + String(data.numNodes) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"lora\":" + String(lora.loraActive ? "true" : "false") + ",";
  json += "\"ble\":true,";
  json += "\"wifi\":true,";
  json += "\"espnow\":" + String(espnow.espNowActive ? "true" : "false") + ",";

  json += "\"log\":[";
  bool first = true;
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (data.logIndex - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (data.msgLog[idx].length() > 0) {
      String sanitized = data.msgLog[idx];
      sanitized.replace("\"", "'");
      if (!first)
        json += ",";
      json += "\"" + sanitized + "\"";
      first = false;
    }
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void WiFiManager::serveApiCmd() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    if (_webCmdCallback) {
      _webCmdCallback(cmd, CommInterface::WIFI);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing cmd");
  }
}

void WiFiManager::serveApiPeers() {
  DataManager &data = DataManager::getInstance();

  String json = "[";
  bool first = true;
  for (int i = 0; i < data.numEspNowPeers; i++) {
    if (data.espNowPeers[i].active) {
      if (!first)
        json += ",";
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              data.espNowPeers[i].mac[0], data.espNowPeers[i].mac[1],
              data.espNowPeers[i].mac[2], data.espNowPeers[i].mac[3],
              data.espNowPeers[i].mac[4], data.espNowPeers[i].mac[5]);
      json += "{\"mac\":\"" + String(macStr) + "\",\"name\":\"" +
              String(data.espNowPeers[i].name) + "\"}";
      first = false;
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

void WiFiManager::serveApiAddPeer() {
  if (!server.hasArg("mac") || !server.hasArg("name")) {
    server.send(400, "text/plain", "Missing mac or name");
    return;
  }

  String macStr = server.arg("mac");
  String name = server.arg("name");

  // Parse MAC "AA:BB:CC:DD:EE:FF"
  uint8_t mac[6];
  int parsed = sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0],
                      &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  if (parsed != 6) {
    server.send(400, "text/plain", "Invalid MAC format");
    return;
  }

  bool ok = ESPNowManager::getInstance().addPeer(mac, name.c_str());
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Failed");
}

void WiFiManager::serveApiRemovePeer() {
  if (!server.hasArg("mac")) {
    server.send(400, "text/plain", "Missing mac");
    return;
  }

  String macStr = server.arg("mac");
  uint8_t mac[6];
  int parsed = sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0],
                      &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  if (parsed != 6) {
    server.send(400, "text/plain", "Invalid MAC format");
    return;
  }

  bool ok = ESPNowManager::getInstance().removePeer(mac);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Failed");
}
