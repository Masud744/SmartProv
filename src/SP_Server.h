/**
 * SP_Server.h — SmartProv Web Server and Captive Portal Module
 * =============================================================
 * Hosts the provisioning HTTP server and DNS captive portal redirect
 * during AP mode. Renders the setup UI, accepts form submissions,
 * validates input, and invokes the configuration callback.
 *
 * HTML is stored in PROGMEM and streamed via chunked response to
 * minimize RAM usage on ESP8266. Custom fields registered via
 * addFieldDef() are injected dynamically between PROGMEM chunks.
 *
 * Optional OTA firmware upload via enableOTA() before begin().
 */

#ifndef SP_SERVER_H
#define SP_SERVER_H

#include <Arduino.h>
#include "SP_Storage.h"

#ifdef ESP32
    #include <WebServer.h>
    #include <DNSServer.h>
    #include <Update.h>
    #define SP_WebServerClass WebServer
#else
    #include <ESP8266WebServer.h>
    #include <DNSServer.h>
    #include <Updater.h>
    #define SP_WebServerClass ESP8266WebServer
#endif

#ifndef SP_VERSION
    #define SP_VERSION "3.0.0"
#endif

#define DNS_PORT 53

typedef void (*SPConfigCallback)(SPConfig);

struct SPFieldDef {
    char key[SP_FIELD_KEY_LEN];
    char label[32];
    char placeholder[48];
    bool valid;
};

// ---------------------------------------------------------------------------
// PROGMEM HTML chunks — setup page
// ---------------------------------------------------------------------------

static const char SP_HTML_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>SmartProv Setup</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0f1419;--card:#161b22;--border:#30363d;
  --accent:#0284c7;--accent-hover:#0369a1;
  --text:#e6edf3;--muted:#7d8590;
  --input-bg:#0d1117;
}
html{height:100%}
body{
  font-family:system-ui,sans-serif;
  background:var(--bg);color:var(--text);min-height:100vh;
  display:flex;align-items:flex-start;justify-content:center;
  padding:16px 16px 40px;
}
.card{
  width:100%;max-width:420px;
  background:var(--card);border:1px solid var(--border);
  border-radius:8px;overflow:hidden;
  box-shadow:0 1px 3px rgba(0,0,0,.3);
}
.brand{
  padding:20px 24px;display:flex;align-items:center;gap:12px;
  border-bottom:1px solid var(--border);
}
.brand-icon{
  width:40px;height:40px;border-radius:8px;
  background:var(--accent);display:flex;align-items:center;justify-content:center;
  flex-shrink:0;
}
.brand-icon svg{width:22px;height:22px;fill:#fff}
.brand-text h1{font-size:17px;font-weight:600}
.brand-text p{font-size:12px;color:var(--muted);margin-top:1px}
.body{padding:20px}
.field{margin-bottom:16px}
label{
  display:block;font-size:11px;font-weight:600;
  letter-spacing:.5px;text-transform:uppercase;
  color:var(--muted);margin-bottom:6px;
}
label .hint{font-weight:400;text-transform:none;letter-spacing:0;opacity:.7;margin-left:6px}
select,input[type=text],input[type=password]{
  width:100%;background:var(--input-bg);border:1px solid var(--border);
  border-radius:6px;padding:10px 12px;font-size:14px;
  color:var(--text);outline:none;-webkit-appearance:none;appearance:none;
  transition:border-color .15s;
}
select:focus,input:focus{border-color:var(--accent)}
input::placeholder{color:var(--muted);opacity:.5}
.select-wrap{position:relative}
.select-wrap::after{
  content:'';position:absolute;right:12px;top:50%;transform:translateY(-50%);
  border:5px solid transparent;border-top:6px solid var(--muted);
  pointer-events:none;margin-top:3px;
}
.pw-wrap{position:relative}
.pw-wrap input{padding-right:42px}
.pw-toggle{
  position:absolute;right:0;top:0;height:100%;width:42px;
  background:none;border:none;color:var(--muted);cursor:pointer;
  display:flex;align-items:center;justify-content:center;
}
.pw-toggle svg{width:16px;height:16px;fill:currentColor}
.net-meta{display:none;gap:8px;margin-top:8px;flex-wrap:wrap}
.net-meta.show{display:flex}
.badge{font-size:11px;padding:2px 8px;border-radius:4px;font-weight:500}
.badge-signal{background:rgba(16,185,129,.1);color:#6ee7b7;border:1px solid rgba(16,185,129,.2)}
.badge-lock{background:rgba(245,158,11,.1);color:#fcd34d;border:1px solid rgba(245,158,11,.2)}
.badge-open{background:rgba(100,116,139,.1);color:#94a3b8;border:1px solid rgba(100,116,139,.2)}
.field-hint{font-size:12px;color:var(--muted);margin-top:5px;line-height:1.5}
.section-label{
  margin:20px 0 14px;font-size:11px;text-transform:uppercase;
  letter-spacing:.5px;font-weight:600;color:var(--muted);
}
.add-net-btn{
  width:100%;background:none;border:1px solid var(--border);
  border-radius:6px;padding:10px;color:var(--muted);
  font-size:13px;cursor:pointer;margin-bottom:16px;
  transition:border-color .15s,color .15s;
}
.add-net-btn:hover{border-color:var(--accent);color:var(--accent)}
.net-section{display:none}
.net-section.show{display:block}
.row-flex{display:flex;gap:8px;align-items:center}
.row-flex .select-wrap{flex:1}
.rescan-btn{
  width:38px;height:38px;border-radius:6px;border:1px solid var(--border);
  background:var(--input-bg);color:var(--muted);cursor:pointer;
  display:flex;align-items:center;justify-content:center;flex-shrink:0;
  transition:border-color .15s,color .15s;
}
.rescan-btn:hover{border-color:var(--accent);color:var(--accent)}
.rescan-btn svg{width:14px;height:14px;fill:currentColor}
.error-box{
  background:rgba(239,68,68,.08);border:1px solid rgba(239,68,68,.25);
  border-radius:6px;padding:10px 12px;font-size:13px;color:#fca5a5;
  margin-bottom:16px;display:none;
}
.error-box.show{display:block}
.connecting-box{
  background:rgba(2,132,199,.08);border:1px solid rgba(2,132,199,.2);
  border-radius:6px;padding:12px;font-size:13px;color:#7dd3fc;
  margin-bottom:16px;display:none;align-items:center;gap:10px;
}
.connecting-box.show{display:flex}
.spinner{
  width:16px;height:16px;border:2px solid rgba(2,132,199,.2);
  border-top-color:var(--accent);border-radius:50%;
  animation:spin .7s linear infinite;flex-shrink:0;
}
@keyframes spin{to{transform:rotate(360deg)}}
.btn{
  width:100%;background:var(--accent);color:#fff;border:none;
  border-radius:6px;padding:12px;font-size:14px;
  font-weight:600;cursor:pointer;margin-top:4px;
  transition:background .15s;
}
.btn:hover{background:var(--accent-hover)}
.btn:active{opacity:.9}
.btn:disabled{opacity:.5;cursor:not-allowed}
.reset-hint{text-align:center;margin-top:14px;font-size:11px;color:var(--muted);line-height:1.5}
.footer{
  text-align:center;padding:12px;border-top:1px solid var(--border);
  font-size:11px;color:var(--muted);
}
</style>
</head>
<body>
<div class="card">

  <div class="brand">
    <div class="brand-icon">
      <svg viewBox="0 0 24 24"><path d="M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3a4.24 4.24 0 00-6 0zm-4-4l2 2a7.07 7.07 0 0110 0l2-2C15.14 9.14 8.87 9.14 5 13z"/></svg>
    </div>
    <div class="brand-text">
      <h1>SmartProv</h1>
      <p>Device Setup</p>
    </div>
  </div>

  <div class="body">

    <div class="error-box" id="errBox"><span id="errMsg"></span></div>
    <div class="connecting-box" id="connectingBox">
      <div class="spinner"></div>
      <span id="connectingMsg">Saving credentials&hellip;</span>
    </div>

    <div class="field">
      <label>WiFi Network</label>
      <div class="row-flex">
        <div class="select-wrap">
          <select id="ssidSelect" onchange="onNetSelect(this)">
            <option value="">Loading networks&hellip;</option>
          </select>
        </div>
        <button type="button" class="rescan-btn" onclick="rescanNets()" title="Refresh networks">
          <svg viewBox="0 0 16 16"><path d="M13.65 2.35a8 8 0 10.6 11.23.75.75 0 00-1.12-1A6.5 6.5 0 1112.5 4.5H10a.75.75 0 000 1.5h4a.75.75 0 00.75-.75v-4a.75.75 0 00-1.1-.65z"/></svg>
        </button>
      </div>
      <div class="net-meta" id="netMeta">
        <span class="badge badge-signal" id="metaSignal"></span>
        <span class="badge" id="metaSec"></span>
      </div>
    </div>

    <div class="field" id="manualField" style="display:none">
      <label>Network Name (SSID)</label>
      <input type="text" id="manualSsid" placeholder="Enter WiFi name" maxlength="32" autocomplete="off">
    </div>

    <input type="hidden" id="ssidHidden">

    <div class="field">
      <label>Password</label>
      <div class="pw-wrap">
        <input type="password" id="pw1" placeholder="WiFi password" maxlength="63" autocomplete="off">
        <button type="button" class="pw-toggle" onclick="togglePw('pw1')" aria-label="Show password">
          <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zm0 12.5c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>
        </button>
      </div>
      <div class="field-hint" id="pw1Hint" style="display:none">Open network — no password required.</div>
    </div>

    <button type="button" class="add-net-btn" id="backup1Btn" onclick="toggleNet(1)">
      + Add backup network
    </button>

    <div class="net-section" id="net1Section">
      <div class="field">
        <label>Backup SSID</label>
        <input type="text" id="ssid2" placeholder="Second network" maxlength="32" autocomplete="off">
      </div>
      <div class="field">
        <label>Backup Password</label>
        <div class="pw-wrap">
          <input type="password" id="pw2" placeholder="Password" maxlength="63" autocomplete="off">
          <button type="button" class="pw-toggle" onclick="togglePw('pw2')" aria-label="Show password">
            <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zm0 12.5c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>
          </button>
        </div>
      </div>
      <button type="button" class="add-net-btn" id="backup2Btn" onclick="toggleNet(2)">
        + Add third network
      </button>
    </div>

    <div class="net-section" id="net2Section">
      <div class="field">
        <label>Third SSID</label>
        <input type="text" id="ssid3" placeholder="Third network" maxlength="32" autocomplete="off">
      </div>
      <div class="field">
        <label>Third Password</label>
        <div class="pw-wrap">
          <input type="password" id="pw3" placeholder="Password" maxlength="63" autocomplete="off">
          <button type="button" class="pw-toggle" onclick="togglePw('pw3')" aria-label="Show password">
            <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zm0 12.5c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>
          </button>
        </div>
      </div>
    </div>

    <div class="section-label">Device Settings</div>

    <div class="field">
      <label>Device Name <span class="hint">(optional)</span></label>
      <input type="text" id="devName" placeholder="e.g. Living Room Sensor" maxlength="32" autocomplete="off">
      <div class="field-hint">Used to identify this device on your network.</div>
    </div>
)rawliteral";

// Injected: custom fields (dynamic, non-PROGMEM)

static const char SP_HTML_TAIL[] PROGMEM = R"rawliteral(
    <button type="button" class="btn" id="submitBtn" onclick="submitForm()">Save &amp; Connect</button>
    <p class="reset-hint">Hold reset for 3s to factory reset.</p>

  </div>
  <div class="footer">SmartProv v3.0.0</div>
</div>

<script>
var _nets=[];

function sigLabel(s){return['','Weak','Fair','Good','Excellent'][s]||'';}

function loadNets(){
  var sel=document.getElementById('ssidSelect');
  sel.innerHTML='<option value="">Loading networks\u2026</option>';
  fetch('/networks')
    .then(function(r){return r.json();})
    .then(function(nets){
      _nets=nets;
      sel.innerHTML='<option value="">-- Select network --</option>';
      nets.forEach(function(n,i){
        var o=document.createElement('option');
        o.value=String(i);
        o.textContent=n.ssid+' ('+sigLabel(n.signal)+')';
        sel.appendChild(o);
      });
      var m=document.createElement('option');
      m.value='__manual__';m.textContent='Enter manually\u2026';
      sel.appendChild(m);
    })
    .catch(function(){
      sel.innerHTML='<option value="__manual__">Enter network name manually</option>';
      onNetSelect(sel);
    });
}

window.onload=loadNets;

function rescanNets(){loadNets();}

function onNetSelect(el){
  var mf=document.getElementById('manualField');
  var sh=document.getElementById('ssidHidden');
  var meta=document.getElementById('netMeta');
  var hint=document.getElementById('pw1Hint');
  if(el.value==='__manual__'){
    mf.style.display='block';sh.value='';meta.className='net-meta';hint.style.display='none';
  } else if(el.value===''){
    mf.style.display='none';sh.value='';meta.className='net-meta';hint.style.display='none';
  } else {
    var n=_nets[parseInt(el.value,10)];
    mf.style.display='none';sh.value=n.ssid;meta.className='net-meta show';
    document.getElementById('metaSignal').textContent=sigLabel(n.signal);
    var sec=document.getElementById('metaSec');
    sec.className='badge '+(n.open?'badge-open':'badge-lock');
    sec.textContent=n.open?'Open':'Secured';
    hint.style.display=n.open?'block':'none';
  }
}

function togglePw(id){var e=document.getElementById(id);e.type=e.type==='password'?'text':'password';}

function toggleNet(n){
  if(n===1){
    var s=document.getElementById('net1Section');
    var b=document.getElementById('backup1Btn');
    var on=!s.classList.contains('show');
    s.className=on?'net-section show':'net-section';
    b.textContent=on?'- Remove backup network':'+ Add backup network';
    if(!on){
      document.getElementById('ssid2').value='';document.getElementById('pw2').value='';
      var s2=document.getElementById('net2Section');
      s2.className='net-section';
      document.getElementById('backup2Btn').textContent='+ Add third network';
      document.getElementById('ssid3').value='';document.getElementById('pw3').value='';
    }
  } else {
    var s=document.getElementById('net2Section');
    var b=document.getElementById('backup2Btn');
    var on=!s.classList.contains('show');
    s.className=on?'net-section show':'net-section';
    b.textContent=on?'- Remove third network':'+ Add third network';
    if(!on){document.getElementById('ssid3').value='';document.getElementById('pw3').value='';}
  }
}

function showErr(msg){
  var b=document.getElementById('errBox');
  document.getElementById('errMsg').textContent=msg;
  b.className='error-box show';
}
function hideErr(){document.getElementById('errBox').className='error-box';}
function setConnecting(msg){
  document.getElementById('connectingMsg').textContent=msg||'Saving credentials\u2026';
  document.getElementById('connectingBox').className='connecting-box show';
  document.getElementById('submitBtn').disabled=true;
}

function submitForm(){
  hideErr();
  var selEl=document.getElementById('ssidSelect');
  if(selEl.value==='__manual__'){
    document.getElementById('ssidHidden').value=document.getElementById('manualSsid').value.trim();
  }
  var ssid=document.getElementById('ssidHidden').value.trim();
  if(!ssid){showErr('Select or enter a WiFi network.');return;}

  var pw=document.getElementById('pw1').value;
  var idx=selEl.value;
  var isOpen=idx!==''&&idx!=='__manual__'&&_nets[parseInt(idx,10)]&&_nets[parseInt(idx,10)].open;
  if(!isOpen&&pw.length>0&&pw.length<8){showErr('Password must be at least 8 characters.');return;}

  var params=new URLSearchParams();
  params.set('ssid',ssid);
  params.set('password',pw);
  params.set('deviceName',document.getElementById('devName').value.trim());
  var s2=document.getElementById('ssid2').value.trim();
  if(s2){params.set('ssid2',s2);params.set('password2',document.getElementById('pw2').value);}
  var s3=document.getElementById('ssid3').value.trim();
  if(s3){params.set('ssid3',s3);params.set('password3',document.getElementById('pw3').value);}
  document.querySelectorAll('input[name]').forEach(function(el){
    if(['ssid','password','deviceName','ssid2','password2','ssid3','password3'].indexOf(el.name)===-1&&el.name){
      params.set(el.name,el.value.trim());
    }
  });

  setConnecting('Saving credentials\u2026');

  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})
    .then(function(r){
      if(r.ok){
        document.getElementById('connectingMsg').textContent='Saved. Restarting\u2026';
        setTimeout(function(){window.location.href='/success';},800);
      } else {
        return r.text().then(function(t){
          document.getElementById('connectingBox').className='connecting-box';
          document.getElementById('submitBtn').disabled=false;
          var m={
            SSID_EMPTY:'Select or enter a WiFi network.',
            SSID_TOO_LONG:'Network name is too long.',
            PASSWORD_TOO_SHORT:'Password must be at least 8 characters.',
            BACKUP_PW_SHORT:'Backup password must be at least 8 characters.'
          };
          showErr(m[t]||'An error occurred.');
        });
      }
    })
    .catch(function(){setTimeout(function(){window.location.href='/success';},600);});
}
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// PROGMEM HTML — success page
// ---------------------------------------------------------------------------

static const char SP_HTML_SUCCESS[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Setup Complete</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:system-ui,sans-serif;
  background:#0f1419;color:#e6edf3;min-height:100vh;
  display:flex;align-items:center;justify-content:center;padding:24px;
}
.card{
  text-align:center;background:#161b22;border:1px solid #30363d;
  border-radius:8px;padding:40px 28px;max-width:360px;width:100%;
  box-shadow:0 1px 3px rgba(0,0,0,.3);
}
.icon{
  width:56px;height:56px;border-radius:50%;margin:0 auto 18px;
  background:rgba(16,185,129,.1);border:1px solid rgba(16,185,129,.25);
  display:flex;align-items:center;justify-content:center;
}
.icon svg{width:28px;height:28px;fill:#6ee7b7}
h1{font-size:20px;font-weight:600;margin-bottom:8px}
.sub{color:#7d8590;font-size:14px;line-height:1.5;margin-bottom:16px}
.note{
  background:rgba(2,132,199,.07);border:1px solid rgba(2,132,199,.15);
  border-radius:6px;padding:12px;font-size:13px;color:#7dd3fc;
  margin-bottom:16px;text-align:left;
}
.counter{font-size:13px;color:#7d8590}
#sec{color:#0284c7;font-weight:600}
.footer{margin-top:20px;font-size:11px;color:#7d8590}
</style>
</head>
<body>
<div class="card">
  <div class="icon">
    <svg viewBox="0 0 24 24"><path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"/></svg>
  </div>
  <h1>Setup Complete</h1>
  <p class="sub">WiFi credentials saved.<br>The device is restarting.</p>
  <div class="note">Reconnect your phone to your regular WiFi network.</div>
  <p class="counter">Restarting in <span id="sec">5</span>s</p>
  <div class="footer">SmartProv v3.0.0</div>
</div>
<script>
var c=5;var t=setInterval(function(){document.getElementById('sec').textContent=--c;if(c<=0)clearInterval(t);},1000);
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// PROGMEM HTML — OTA upload page
// ---------------------------------------------------------------------------

static const char SP_HTML_OTA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:system-ui,sans-serif;
  background:#0f1419;color:#e6edf3;min-height:100vh;
  display:flex;align-items:center;justify-content:center;padding:24px;
}
.card{
  background:#161b22;border:1px solid #30363d;border-radius:8px;
  padding:28px;max-width:400px;width:100%;
  box-shadow:0 1px 3px rgba(0,0,0,.3);
}
h1{font-size:17px;font-weight:600;margin-bottom:16px}
input[type=file]{
  width:100%;padding:10px;background:#0d1117;border:1px solid #30363d;
  border-radius:6px;color:#e6edf3;font-size:13px;margin-bottom:12px;
}
.btn{
  width:100%;background:#0284c7;color:#fff;border:none;border-radius:6px;
  padding:10px;font-size:14px;font-weight:600;cursor:pointer;
}
.btn:hover{background:#0369a1}
#prog{margin-top:12px;font-size:13px;color:#7d8590;display:none}
</style>
</head>
<body>
<div class="card">
  <h1>Firmware Update</h1>
  <form method="POST" action="/update" enctype="multipart/form-data" id="uf">
    <input type="file" name="update" accept=".bin">
    <button type="submit" class="btn">Upload &amp; Flash</button>
  </form>
  <div id="prog"></div>
</div>
<script>
document.getElementById('uf').onsubmit=function(){
  document.getElementById('prog').style.display='block';
  document.getElementById('prog').textContent='Uploading\u2026';
};
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// SP_Server
// ---------------------------------------------------------------------------

class SP_Server {
public:

    SP_Server(SP_WebServerClass& server, DNSServer& dns)
        : _server(server),
          _dns(dns),
          _onConfigReceived(nullptr),
          _wifiScanCallback(nullptr),
          _configSaved(false),
          _otaEnabled(false),
          _fieldDefCount(0)
    {}

    // Register a custom field for the setup form. Call before begin().
    void addFieldDef(const char* key, const char* label, const char* placeholder = "") {
        if (_fieldDefCount >= SP_MAX_FIELDS) return;
        strncpy(_fieldDefs[_fieldDefCount].key,         key,         SP_FIELD_KEY_LEN - 1);
        strncpy(_fieldDefs[_fieldDefCount].label,       label,       31);
        strncpy(_fieldDefs[_fieldDefCount].placeholder, placeholder, 47);
        _fieldDefs[_fieldDefCount].key[SP_FIELD_KEY_LEN - 1] = '\0';
        _fieldDefs[_fieldDefCount].label[31]                  = '\0';
        _fieldDefs[_fieldDefCount].placeholder[47]            = '\0';
        _fieldDefs[_fieldDefCount].valid = true;
        _fieldDefCount++;
    }

    // Set a callback to perform live WiFi rescans from the /networks endpoint.
    void setRescanCallback(String (*cb)()) { _wifiScanCallback = cb; }

    // Enable OTA firmware upload page at /update. Call before begin().
    void enableOTA() { _otaEnabled = true; }

    void begin(IPAddress apIP, const String& networksJson, SPConfigCallback callback) {
        _networksJson     = networksJson;
        _onConfigReceived = callback;
        _configSaved      = false;

        _dns.start(DNS_PORT, "*", apIP);

        _server.on("/",                    HTTP_GET,  [this]() { _handleRoot();     });
        _server.on("/save",                HTTP_POST, [this]() { _handleSave();     });
        _server.on("/networks",            HTTP_GET,  [this]() { _handleNetworks(); });
        _server.on("/success",             HTTP_GET,  [this]() { _handleSuccess();  });
        _server.on("/info",                HTTP_GET,  [this]() { _handleInfo();     });

        // Captive portal detection — redirect probes to setup page
        _server.on("/generate_204",        HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/fwlink",              HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/hotspot-detect.html", HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/connecttest.txt",     HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/ncsi.txt",            HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/canonical.html",      HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/success.txt",         HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.onNotFound([this]() { _redirectToRoot(); });

        if (_otaEnabled) {
            _server.on("/update", HTTP_GET, [this]() { _handleOTAPage(); });
            _server.on("/update", HTTP_POST,
                [this]() { _server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK"); },
                [this]() { _handleOTAUpload(); }
            );
        }

        _server.begin();
        Serial.println(F("[Server] HTTP server started on port 80."));
    }

    void update() {
        _dns.processNextRequest();
        _server.handleClient();
    }

    bool isConfigSaved() const { return _configSaved; }

    void stop() {
        _server.stop();
        _dns.stop();
    }

private:

    // -------------------------------------------------------------------------
    // HTML escaping for XSS protection
    // -------------------------------------------------------------------------

    String _escapeHtml(const char* s) const {
        String out;
        while (*s) {
            switch (*s) {
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '&': out += "&amp;"; break;
                default:  out += *s;
            }
            s++;
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // Request handlers
    // -------------------------------------------------------------------------

    void _handleRoot() {
        _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        _server.sendHeader("Pragma",        "no-cache");
        _server.sendHeader("Expires",       "-1");
        _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server.send(200, "text/html; charset=utf-8", "");
        _server.sendContent_P(SP_HTML_HEAD);
        _server.sendContent(_buildCustomFields());
        _server.sendContent_P(SP_HTML_TAIL);
    }

    void _handleSave() {
        String ssid     = _server.arg("ssid");
        String password = _server.arg("password");
        String devName  = _server.arg("deviceName");

        ssid.trim();
        devName.trim();

        if (ssid.length() == 0) {
            _server.send(400, "text/plain", "SSID_EMPTY");
            return;
        }
        if (ssid.length() > 32) {
            _server.send(400, "text/plain", "SSID_TOO_LONG");
            return;
        }
        if (password.length() > 0 && password.length() < 8) {
            _server.send(400, "text/plain", "PASSWORD_TOO_SHORT");
            return;
        }

        // Validate backup passwords
        String ssid2 = _server.arg("ssid2");
        String pw2   = _server.arg("password2");
        ssid2.trim();
        if (ssid2.length() > 0 && pw2.length() > 0 && pw2.length() < 8) {
            _server.send(400, "text/plain", "BACKUP_PW_SHORT");
            return;
        }

        String ssid3 = _server.arg("ssid3");
        String pw3   = _server.arg("password3");
        ssid3.trim();
        if (ssid3.length() > 0 && pw3.length() > 0 && pw3.length() < 8) {
            _server.send(400, "text/plain", "BACKUP_PW_SHORT");
            return;
        }

        if (devName.length() == 0) devName = "SmartDevice";

        SPConfig config;
        memset(&config, 0, sizeof(config));

        // Primary network
        strncpy(config.networks[0].ssid,     ssid.c_str(),     sizeof(config.networks[0].ssid)     - 1);
        strncpy(config.networks[0].password, password.c_str(), sizeof(config.networks[0].password) - 1);
        config.networks[0].ssid[sizeof(config.networks[0].ssid) - 1] = '\0';
        config.networks[0].password[sizeof(config.networks[0].password) - 1] = '\0';
        config.networks[0].valid = true;

#if SP_MAX_NETWORKS >= 2
        // Backup network 1
        if (ssid2.length() > 0) {
            strncpy(config.networks[1].ssid,     ssid2.c_str(), sizeof(config.networks[1].ssid)     - 1);
            strncpy(config.networks[1].password, pw2.c_str(),   sizeof(config.networks[1].password) - 1);
            config.networks[1].ssid[sizeof(config.networks[1].ssid) - 1] = '\0';
            config.networks[1].password[sizeof(config.networks[1].password) - 1] = '\0';
            config.networks[1].valid = true;
        }
#endif

#if SP_MAX_NETWORKS >= 3
        // Backup network 2
        if (ssid3.length() > 0) {
            strncpy(config.networks[2].ssid,     ssid3.c_str(), sizeof(config.networks[2].ssid)     - 1);
            strncpy(config.networks[2].password, pw3.c_str(),   sizeof(config.networks[2].password) - 1);
            config.networks[2].ssid[sizeof(config.networks[2].ssid) - 1] = '\0';
            config.networks[2].password[sizeof(config.networks[2].password) - 1] = '\0';
            config.networks[2].valid = true;
        }
#endif

        strncpy(config.deviceName, devName.c_str(), sizeof(config.deviceName) - 1);
        config.deviceName[sizeof(config.deviceName) - 1] = '\0';
        config.isConfigured = true;

        for (int i = 0; i < _fieldDefCount; i++) {
            String val = _server.arg(_fieldDefs[i].key);
            val.trim();
            if (val.length() == 0) continue;
            strncpy(config.fields[i].key,   _fieldDefs[i].key, SP_FIELD_KEY_LEN - 1);
            strncpy(config.fields[i].value, val.c_str(),        SP_FIELD_VAL_LEN - 1);
            config.fields[i].key[SP_FIELD_KEY_LEN - 1] = '\0';
            config.fields[i].value[SP_FIELD_VAL_LEN - 1] = '\0';
            config.fields[i].valid = true;
        }

        if (_onConfigReceived) _onConfigReceived(config);
        _configSaved = true;

        _server.send(200, "text/plain", "OK");
    }

    void _handleNetworks() {
        String fresh = _wifiScanCallback ? _wifiScanCallback() : _networksJson;
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", fresh);
    }

    void _handleSuccess() {
        _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server.send(200, "text/html; charset=utf-8", "");
        _server.sendContent_P(SP_HTML_SUCCESS);
    }

    void _handleInfo() {
        String json = "{\"version\":\"" + String(SP_VERSION)
                    + "\",\"heap\":" + String(ESP.getFreeHeap()) + "}";
        _server.send(200, "application/json", json);
    }

    void _handleOTAPage() {
        _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server.send(200, "text/html; charset=utf-8", "");
        _server.sendContent_P(SP_HTML_OTA);
    }

    void _handleOTAUpload() {
        HTTPUpload& upload = _server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.println(F("[OTA] Starting firmware update."));
#ifdef ESP32
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#else
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace)) {
#endif
                Serial.println(F("[OTA] Not enough space."));
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.println(F("[OTA] Write failed."));
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.println(F("[OTA] Update complete."));
            } else {
                Serial.println(F("[OTA] Update failed."));
            }
        }
    }

    void _redirectToRoot() {
        _server.sendHeader("Location", "http://192.168.4.1/");
        _server.send(302, "text/plain", "");
    }

    // -------------------------------------------------------------------------
    // Dynamic HTML builder — custom fields only (small, non-PROGMEM)
    // -------------------------------------------------------------------------

    String _buildCustomFields() const {
        if (_fieldDefCount == 0) return "";
        String out = "<div class=\"section-label\">Advanced Settings</div>";
        for (int i = 0; i < _fieldDefCount; i++) {
            String safeLabel = _escapeHtml(_fieldDefs[i].label);
            String safePlaceholder = _escapeHtml(_fieldDefs[i].placeholder);
            out += "<div class=\"field\">"
                   "<label>" + safeLabel + "</label>"
                   "<input type=\"text\" name=\"" + String(_fieldDefs[i].key) + "\""
                   " placeholder=\"" + safePlaceholder + "\""
                   " maxlength=\"63\" autocomplete=\"off\">"
                   "</div>";
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    SP_WebServerClass&  _server;
    DNSServer&          _dns;
    SPConfigCallback    _onConfigReceived;
    String              (*_wifiScanCallback)();
    String              _networksJson;
    bool                _configSaved;
    bool                _otaEnabled;
    SPFieldDef          _fieldDefs[SP_MAX_FIELDS];
    int                 _fieldDefCount;
};

#endif // SP_SERVER_H
