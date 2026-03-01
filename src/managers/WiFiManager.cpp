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
  lastApiHit = 0;
  modemSleepEnabled = false;
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
          if (ip.fromString(data.staticIp)) {
            if (!gw.fromString(data.gateway)) {
              gw = ip; // Fallback to X.X.X.1
              gw[3] = 1;
            }
            if (!sn.fromString(data.subnet)) {
              sn = IPAddress(255, 255, 255, 0); // Fallback to /24
            }
            WiFi.config(ip, gw, sn);
            Serial.println("WiFi: Using Static IP " + ip.toString());
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
        lastApiHit = millis();
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
  server.on("/", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveHome();
  });

  // Configuration page
  server.on("/config", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveConfig();
  });
  server.on("/config", HTTP_POST, [this]() {
    lastApiHit = millis();
    serveConfigSave();
  });

  // Integration page
  server.on("/integration", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveIntegration();
  });
  server.on("/integration", HTTP_POST, [this]() {
    lastApiHit = millis();
    serveIntegrationSave();
  });

  // Help page
  server.on("/help", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveHelp();
  });

  // API
  server.on("/api/status", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveApiStatus();
  });
  server.on("/api/cmd", HTTP_POST, [this]() {
    lastApiHit = millis();
    serveApiCmd();
  });
  server.on("/api/peers", HTTP_GET, [this]() {
    lastApiHit = millis();
    serveApiPeers();
  });
  server.on("/api/peers/add", HTTP_POST, [this]() {
    lastApiHit = millis();
    serveApiAddPeer();
  });
  server.on("/api/peers/remove", HTTP_POST, [this]() {
    lastApiHit = millis();
    serveApiRemovePeer();
  });

  server.begin();
}

// ============================================================================
//   DASHBOARD PAGE
// ============================================================================
void WiFiManager::serveHome() {
  String html = R"rawhtml(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LoRaLink Dashboard )rawhtml" FIRMWARE_VERSION R"rawhtml(</title>
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
.log{margin:12px;background:#1a1a2e;border-radius:10px;border:1px solid #2a2a4a;max-height:180px;overflow-y:auto}
.ft{position:fixed;bottom:0;left:0;right:0;background:#1a1a2e;border-top:1px solid #2a2a4a;padding:6px;display:flex;flex-wrap:wrap;justify-content:center;gap:3px;z-index:99}
.p{width:8px;height:8px;border-radius:1px;background:#333;cursor:crosshair}
.p.hi{background:#00ff88;box-shadow:0 0 5px #00ff8888}
.p:hover::after{content:attr(t);position:absolute;bottom:12px;background:#000;color:#fff;padding:2px 6px;border-radius:4px;font-size:10px;white-space:nowrap;z-index:100}
.log .m{padding:6px 16px;font-size:0.85em;border-bottom:1px solid #1f1f3a;font-family:monospace}
.cmd{display:flex;gap:8px;padding:16px}
.cmd input{flex:1;background:#1a1a2e;border:1px solid #2a2a4a;border-radius:8px;padding:10px 14px;color:#fff;outline:none}
.cmd input:focus{border-color:#00d4ff}
.cmd button{background:#00d4ff;border:none;border-radius:8px;padding:10px 20px;color:#0f0f1a;cursor:pointer;font-weight:600}
.cmd button:hover{background:#00b8d4}
.ifc{display:flex;gap:6px;padding:8px 16px;flex-wrap:wrap;background:#16213e;border-bottom:1px solid #2a2a4a}
.badge{padding:4px 10px;border-radius:12px;font-size:0.7em;font-weight:650;letter-spacing:0.5px;text-transform:uppercase}
.badge.on{background:#00ff8822;color:#00ff88;border:1px solid #00ff8844;box-shadow:0 0 10px #00ff8811}
.badge.off{background:#ff444422;color:#ff4444;border:1px solid #ff444444}
.pb{padding:2px 6px;border-radius:6px;font-size:0.6em;font-weight:600;border:1px solid #444;font-family:monospace}
.pb.hi{color:#00ff88;border-color:#00ff8833;background:#00ff8808}
.pb.lo{color:#888;border-color:#333;background:#111}
.pb.an{color:#00d4ff;border-color:#00d4ff33;background:#00d4ff08}
.ftl{font-size:0.6em;color:#555;margin-right:8px;text-transform:uppercase;letter-spacing:1px;font-weight:700}
</style></head><body>
<div class='hdr'><h1>&#x1F4E1; LoRaLink Any2Any <span id='fwv'></span></h1><div><a href='/integration'>&#x1F50C; Integrations</a> <a href='/help'>&#x2753; Help</a> <a href='/config'>&#x2699; Config</a></div></div>
<div class='ifc' id='ifc'></div>
<div class='grid' id='cards'></div>
<div class='log'><div class='m' style='color:#00d4ff'>Message Log</div><div id='log'></div></div>
<div class='ft' id='ft'></div>
<div class='cmd'><input id='ci' placeholder='Command...' onkeydown="if(event.key==='Enter')send()"><button onclick='send()'>Send</button></div>
<script>
function up(){fetch('/api/status').then(r=>r.json()).then(d=>{
document.getElementById('ifc').innerHTML=
`<span class="badge ${d.lora?'on':'off'}">LoRa</span>`+
`<span class="badge ${d.ble?'on':'off'}">BLE</span>`+
`<span class="badge ${d.wifi?'on':'off'}">WiFi</span>`+
`<span class="badge ${d.espnow?'on':'off'}">ESP-NOW</span>`;

let m=BigInt("0x"+d.gp),f='<div class="ftl">Logic Map</div>';
for(let i=0;i<48;i++){
  let h=(m >> BigInt(i)) & 1n;
  f+=`<div class="p ${h?'hi':''}" t="P${i}:${h?'HI':'LO'}" style="position:relative"></div>`;
}
document.getElementById('ft').innerHTML=f;

let c=`<div class="card"><div class="lbl">Device</div><div class="val">${d.id} <span style="font-size:0.6em;color:#666">(${d.hw})</span></div></div>`;
document.getElementById('fwv').innerText=d.version;
c+=`<div class="card"><div class="lbl">Uptime</div><div class="val">${d.uptime}</div></div>`;
c+=`<div class="card"><div class="lbl">Battery</div><div class="val ${d.bat>3.5?'ok':'warn'}">${d.bat}V</div></div>`;
c+=`<div class="card"><div class="lbl">Reset</div><div class="val" style="font-size:0.9em">${d.reset}</div></div>`;
c+=`<div class="card"><div class="lbl">LoRa RSSI</div><div class="val">${d.rssi} dBm</div></div>`;
c+=`<div class="card"><div class="lbl">Nodes</div><div class="val">${d.nodes}</div></div>`;
c+=`<div class="card"><div class="lbl">Heap</div><div class="val">${d.heap}</div></div>`;
c+=`<div class="card" style="border-color:#00ff8844"><div class="lbl">ESPNOW RX/TX</div><div class="val">${d.espnow_rx} / ${d.espnow_tx}</div></div>`;
c+=`<div class="card" style="border-color:${d.espnow_ok?'#00ff8844':'#ff444444'}"><div class="lbl">ESPNOW Status</div><div class="val ${d.espnow_ok?'ok':'warn'}">${d.espnow_ok?'OK':'FAIL'}</div></div>`;
c+=`<div class="card"><div class="lbl">Last Command</div><div class="val" style="font-size:0.7em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis" title="${d.last_cmd}">${d.last_cmd||'-'}</div></div>`;
document.getElementById('cards').innerHTML=c;
let l='';d.log.forEach(m=>{if(m)l+=`<div class="m"><span style="color:#888">[${m.ts}s]</span> <span style="color:#00d4ff">${m.src}</span>: ${m.msg}</div>`;});
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
<title>LoRaLink Config )rawhtml" FIRMWARE_VERSION R"rawhtml(</title>
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
<div class='hdr'><h1>&#x2699; Configuration</h1><div><a href='/'>&#x1F4E1; Dashboard</a> <a href='/integration'>&#x1F50C; Integrations</a></div></div>
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
//   HELP PAGE
// ============================================================================
void WiFiManager::serveHelp() {
  String html = R"rawhtml(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LoRaLink Help </title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f0f1a;color:#e0e0e0;min-height:100vh}
.hdr{background:linear-gradient(135deg,#1a1a2e,#16213e);padding:16px 20px;border-bottom:1px solid #2a2a4a;display:flex;justify-content:space-between;align-items:center}
.hdr h1{font-size:1.3em;color:#00d4ff;font-weight:600}
.hdr a{color:#888;text-decoration:none;font-size:0.85em;padding:6px 14px;border:1px solid #2a2a4a;border-radius:6px}
.hdr a:hover{color:#00d4ff;border-color:#00d4ff}
.content{padding:20px;max-width:800px;margin:0 auto}
h2{color:#00d4ff;margin-top:20px;margin-bottom:10px;font-size:1.2em;border-bottom:1px solid #2a2a4a;padding-bottom:5px}
p{margin-bottom:10px;line-height:1.5}
table{width:100%;border-collapse:collapse;margin-bottom:20px;background:#1a1a2e;border-radius:8px;overflow:hidden;box-shadow:0 4px 6px rgba(0,0,0,0.1)}
th,td{padding:12px 15px;text-align:left;border-bottom:1px solid #2a2a4a}
th{background:#16213e;color:#00d4ff;font-weight:600}
tr:last-child td{border-bottom:none}
pre{background:#000;padding:10px;border-radius:6px;overflow-x:auto;margin-bottom:15px;border:1px solid #333}
code{font-family:monospace;color:#00ff88;background:#00ff8811;padding:2px 6px;border-radius:4px}
ul{margin-left:20px;margin-bottom:15px}
li{margin-bottom:5px;line-height:1.4}
</style></head><body>
<div class='hdr'><h1>&#x2753; Command Reference</h1><div><a href='/'>&#x1F4E1; Dashboard</a> <a href='/config'>&#x2699; Config</a></div></div>
<div class='content'>
  <p>The <code>CommandManager</code> routes commands uniformly regardless of the interface they are received on (Serial, LoRa, BLE, or Web UI).</p>
  
  <h2>Command Routing</h2>
  <ul>
    <li><b>Local:</b> Type the command directly (e.g., <code>STATUS</code>).</li>
    <li><b>Targeted:</b> Prefix the command with a node's Name or MAC Suffix (e.g., <code>Master STATUS</code> or <code>E4 STATUS</code>).</li>
    <li><b>Broadcast:</b> Prefix the command with <code>ALL</code> (e.g., <code>ALL STATUS</code>). Every node will execute it.</li>
  </ul>

  <h2>Global / System Commands</h2>
  <table>
    <tr><th>Command</th><th>Arguments</th><th>Description</th></tr>
    <tr><td><code>SETNAME</code></td><td><code>&lt;name&gt;</code></td><td>Sets friendly display name (1-14 chars). Reboots.</td></tr>
    <tr><td><code>SETWIFI</code></td><td><code>&lt;ssid&gt; &lt;pass&gt;</code></td><td>Sets WiFi credentials. Reboots.</td></tr>
    <tr><td><code>SETIP</code></td><td><code>&lt;ip&gt;</code> / <code>OFF</code></td><td>Sets static IP or returns to DHCP. Reboots.</td></tr>
    <tr><td><code>ESPNOW</code></td><td><code>ON</code> / <code>OFF</code></td><td>Enables/disables high-speed peer network. Reboots.</td></tr>
    <tr><td><code>REPEATER</code></td><td><code>ON</code> / <code>OFF</code></td><td>Enables/disables LoRa repeater mode.</td></tr>
    <tr><td><code>SLEEP</code></td><td><code>&lt;hours&gt;</code></td><td>Deep sleep for X hours.</td></tr>
    <tr><td><code>SETKEY</code></td><td><code>&lt;32_hex&gt;</code></td><td>Sets AES-128 key (32 hex chars). Reboots.</td></tr>
    <tr><td><code>WIPECONFIG</code></td><td><i>(none)</i></td><td>Factory resets all settings. Reboots.</td></tr>
  </table>

  <h2>Action & Diagnostic Commands</h2>
  <table>
    <tr><th>Command</th><th>Arguments</th><th>Description</th></tr>
    <tr><td><code>STATUS</code></td><td><i>(none)</i></td><td>Returns battery, IP, LoRa RSSI, etc.</td></tr>
    <tr><td><code>RADIO</code></td><td><i>(none)</i></td><td>Dumps LoRa diagnostic info to Serial.</td></tr>
    <tr><td><code>READMAC</code></td><td><i>(none)</i></td><td>Returns raw WiFi MAC address.</td></tr>
    <tr><td><code>BLINK</code></td><td><i>(none)</i></td><td>Blinks the onboard LED.</td></tr>
    <tr><td><code>LED</code></td><td><code>ON</code> / <code>OFF</code></td><td>Turns the built-in LED on/off.</td></tr>
    <tr><td><code>GPIO</code></td><td><code>&lt;pin/name&gt; &lt;1/0&gt;</code></td><td>Sets a specific GPIO pin HIGH/LOW.</td></tr>
    <tr><td><code>READ</code></td><td><code>&lt;pin/name&gt;</code></td><td>Reads digital state of a pin.</td></tr>
    <tr><td><code>GETSCHED</code></td><td><i>(none)</i></td><td>Dumps saved JSON schedule.</td></tr>
    <tr><td><code>SETSCHED</code></td><td><code>&lt;json&gt;</code></td><td>Uploads a new JSON schedule.</td></tr>
    <tr><td><code>HELP</code></td><td><i>(none)</i></td><td>Prints a short list of commands on Serial.</td></tr>
  </table>

  <h2>Hidden / Debug Commands</h2>
  <ul>
    <li><code>INJECT &lt;cmd&gt;</code>: Simulates receiving a LoRa packet containing a command.</li>
  </ul>

  <h2>Chat Messaging</h2>
  <p>Any text that does not match a known command is treated as a Chat Message. It is displayed on the OLED and transmitted via LoRa to all nodes.</p>
</div>
</body></html>)rawhtml";
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
  json += "\"version\":\"" FIRMWARE_VERSION "\",";
  json += "\"uptime\":\"" + uptime + "\",";
  json += "\"espnow_ch\":" + String(data.espNowChannel) + ",";
  json += "\"espnow_rx\":" + String(ESPNowManager::getInstance().rxCount) + ",";
  json += "\"espnow_tx\":" + String(ESPNowManager::getInstance().txCount) + ",";
  json +=
      "\"espnow_ok\":" + String(ESPNowManager::getInstance().lastSendSuccess) +
      ",";
  json += "\"espnow_peers\":" + String(data.numEspNowPeers) + ",";
  json += "\"hw\":\"" + data.getMacSuffix() + "\",";
  json += "\"reset\":\"" + data.getResetReason() + "\",";
  json += "\"last_cmd\":\"" + lora.lastMsgReceived + "\",";
  json += "\"bat\":" + String(bat, 2) + ",";
  json += "\"rssi\":" + String(lora.lastRssi) + ",";
  json += "\"nodes\":" + String(data.numNodes) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"lora\":" + String(lora.loraActive ? "true" : "false") + ",";
  json += "\"ble\":true,";
  json += "\"wifi\":true,";
  json += "\"espnow\":" + String(espnow.espNowActive ? "true" : "false") + ",";

  // Standard pins array for labeled diagnostics
  json += "\"pins\":[";
  struct PinDef {
    const char *n;
    int p;
    bool a;
  };
  PinDef pins[] = {
      {"LED", PIN_LED_BUILTIN, false},    {"PRG", PIN_BUTTON_PRG, false},
      {"VEXT", PIN_VEXT_CTRL, false},     {"BAT", PIN_BAT_ADC, true},
      {"RLY1", PIN_RELAY_110V, false},    {"RL12_1", PIN_RELAY_12V_1, false},
      {"RL12_2", PIN_RELAY_12V_2, false}, {"RL12_3", PIN_RELAY_12V_3, false}};
  for (int i = 0; i < 8; i++) {
    if (i > 0)
      json += ",";
    json += "{\"n\":\"" + String(pins[i].n) +
            "\",\"v\":" + String(digitalRead(pins[i].p));
    if (pins[i].a)
      json += ",\"a\":" + String(analogRead(pins[i].p));
    json += "}";
  }
  json += "],";

  // Comprehensive GPIO Bitmask (0-47)
  uint64_t mask = 0;
  for (int i = 0; i < 48; i++) {
    // Skip flash/invalid pins to prevent crashes
    if (i >= 26 && i <= 32)
      continue;
    if (digitalRead(i))
      mask |= (1ULL << i);
  }
  char hex[17];
  sprintf(hex, "%012llX", mask);
  json += "\"gp\":\"" + String(hex) + "\",";

  // Mesh neighbor table
  json += "\"mesh\":[";
  bool meshFirst = true;
  unsigned long now = millis();
  for (int i = 0; i < data.numNodes; i++) {
    if (!meshFirst)
      json += ",";
    json += "{";
    json += "\"id\":\"" + String(data.remoteNodes[i].id) + "\",";
    json += "\"bat\":" + String(data.remoteNodes[i].battery, 2) + ",";
    json += "\"rssi\":" + String(data.remoteNodes[i].rssi) + ",";
    json += "\"hops\":" + String(data.remoteNodes[i].hops) + ",";
    json += "\"ago\":" + String((now - data.remoteNodes[i].lastSeen) / 1000);
    json += "}";
    meshFirst = false;
  }
  json += "],";

  json += "\"log\":[";
  bool first = true;
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (data.logIndex - 1 - i + LOG_SIZE) % LOG_SIZE;
    if (data.msgLog[idx].message.length() > 0) {
      String sanitized = data.msgLog[idx].message;
      sanitized.replace("\"", "'");
      String cleanStr = "";
      for (unsigned int j = 0; j < sanitized.length(); j++) {
        char c = sanitized[j];
        if (c >= 0x20 && c <= 0x7E) {
          cleanStr += c;
        }
      }
      if (!first)
        json += ",";

      json += "{";
      json += "\"ts\":" + String(data.msgLog[idx].timestamp) + ",";
      json += "\"src\":\"" + data.msgLog[idx].source + "\",";
      json += "\"rssi\":" + String(data.msgLog[idx].rssi) + ",";
      json += "\"msg\":\"" + cleanStr + "\"";
      json += "}";

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
      _webCmdCallback(cmd, CommInterface::COMM_WIFI);
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

// ============================================================================
//   INTEGRATION PAGE
// ============================================================================
void WiFiManager::serveIntegration() {
  DataManager &data = DataManager::getInstance();

  String html = R"rawhtml(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>LoRaLink Integrations</title>
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
.row label{width:160px;font-size:0.85em;color:#aaa;flex-shrink:0}
.row input,.row select{flex:1;background:#0f0f1a;border:1px solid #2a2a4a;border-radius:6px;padding:8px 12px;color:#fff;outline:none;min-width:140px}
.row input:focus,.row select:focus{border-color:#00d4ff}
.btn{background:#00d4ff;border:none;border-radius:8px;padding:10px 24px;color:#0f0f1a;cursor:pointer;font-weight:600;margin-top:12px}
.btn:hover{background:#00b8d4}
.msg{background:#00ff8822;color:#00ff88;border:1px solid #00ff8844;border-radius:8px;padding:10px 16px;margin:16px;text-align:center;display:none}
</style></head><body>
<div class='hdr'><h1>&#x1F50C; Integrations</h1><div><a href='/'>&#x1F4E1; Dashboard</a> <a href='/config'>&#x2699; Config</a></div></div>
)rawhtml";

  if (server.hasArg("saved")) {
    html += "<div class='msg' style='display:block'>Settings saved. "
            "Rebooting...</div>";
  }

  html += R"rawhtml(<form method='POST' action='/integration'>

<div class='sec'>
<h2>&#x1F4CA; Excel Data Streamer</h2>
<p style='font-size:0.85em;color:#888;margin-bottom:12px'>Outputs live telemetry and messages to the USB Serial port in CSV format.</p>
<div class='row'><label>Serial CSV Stream</label><select name='stream'><option value='0')rawhtml";
  if (!data.streamToSerial)
    html += " selected";
  html += R"rawhtml(>OFF</option><option value='1')rawhtml";
  if (data.streamToSerial)
    html += " selected";
  html += R"rawhtml(>ON</option></select></div>
</div>

<div class='sec'>
<h2>&#x1F310; MQTT Broker</h2>
<p style='font-size:0.85em;color:#888;margin-bottom:12px'>Publishes telemetry to <code>loralink/telemetry/&lt;Node&gt;</code> and messages to <code>loralink/msg/&lt;Node&gt;</code>.</p>
<div class='row'><label>MQTT Enabled</label><select name='mqtt_en'><option value='0')rawhtml";
  if (!data.mqttEnabled)
    html += " selected";
  html += R"rawhtml(>OFF</option><option value='1')rawhtml";
  if (data.mqttEnabled)
    html += " selected";
  html +=
      R"rawhtml(>ON</option></select></div>
<div class='row'><label>Server Address</label><input name='mqtt_srv' value=')rawhtml" +
      data.mqttServer +
      R"rawhtml(' placeholder='e.g. 192.168.1.100 or broker.hivemq.com'></div>
<div class='row'><label>Server Port</label><input name='mqtt_port' type='number' value=')rawhtml" +
      String(data.mqttPort) + R"rawhtml('></div>
<div class='row'><label>Username (Optional)</label><input name='mqtt_user' value=')rawhtml" +
      data.mqttUser + R"rawhtml('></div>
<div class='row'><label>Password (Optional)</label><input name='mqtt_pass' type='password' value=')rawhtml" +
      data.mqttPass + R"rawhtml('></div>
</div>

<div class='sec'>
<button type='submit' class='btn'>&#x1F4BE; Save & Reboot</button>
</div>

</form>
</body></html>)rawhtml";

  server.send(200, "text/html", html);
}

void WiFiManager::serveIntegrationSave() {
  DataManager &data = DataManager::getInstance();

  if (server.hasArg("stream")) {
    data.streamToSerial = server.arg("stream") == "1";
    // Also trigger it locally in CommandManager if we are not rebooting
    // immediately
  }

  if (server.hasArg("mqtt_en")) {
    bool en = server.arg("mqtt_en") == "1";
    String srv = server.hasArg("mqtt_srv") ? server.arg("mqtt_srv") : "";
    int port =
        server.hasArg("mqtt_port") ? server.arg("mqtt_port").toInt() : 1883;
    String user = server.hasArg("mqtt_user") ? server.arg("mqtt_user") : "";
    String pass = server.hasArg("mqtt_pass") ? server.arg("mqtt_pass") : "";

    data.SetMqtt(en, srv, port, user, pass);
  }

  data.SaveSettings();

  server.sendHeader("Location", "/integration?saved=1");
  server.send(303);

  // Reboot to apply changes cleanly like in serveConfigSave
  delay(1000);
  ESP.restart();
}
