/**
 * SP_Server.h — SmartProv Web Server and Captive Portal Module
 * =============================================================
 * Hosts the provisioning HTTP server and DNS captive portal redirect
 * during AP mode. Renders the setup UI, accepts form submissions,
 * validates input, and invokes the configuration callback.
 *
 * Custom fields registered via addFieldDef() are injected into the
 * rendered HTML form at runtime and parsed from POST submissions.
 *
 * Captive portal detection endpoints for Android, iOS, Windows, and
 * Linux are all handled with redirect responses to the root page.
 *
 * Form submissions use application/x-www-form-urlencoded encoding,
 * which is reliably parsed by both ESP32 WebServer and ESP8266WebServer.
 */

#ifndef SP_SERVER_H
#define SP_SERVER_H

#include <Arduino.h>
#include "SP_Storage.h"

#ifdef ESP32
    #include <WebServer.h>
    #include <DNSServer.h>
    #define SP_WebServerClass WebServer
#else
    #include <ESP8266WebServer.h>
    #include <DNSServer.h>
    #define SP_WebServerClass ESP8266WebServer
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
// SP_Server
// ---------------------------------------------------------------------------

class SP_Server {
public:

    SP_Server(SP_WebServerClass& server, DNSServer& dns)
        : _server(server),
          _dns(dns),
          _onConfigReceived(nullptr),
          _configSaved(false),
          _fieldDefCount(0)
    {}

    // Registers a custom field to appear in the setup form.
    // Must be called before begin().
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

    void begin(IPAddress apIP, const String& networksJson, SPConfigCallback callback) {
        _networksJson     = networksJson;
        _onConfigReceived = callback;
        _configSaved      = false;

        _dns.start(DNS_PORT, "*", apIP);

        _server.on("/",                    HTTP_GET,  [this]() { _handleRoot();     });
        _server.on("/save",                HTTP_POST, [this]() { _handleSave();     });
        _server.on("/networks",            HTTP_GET,  [this]() { _handleNetworks(); });
        _server.on("/success",             HTTP_GET,  [this]() { _handleSuccess();  });

        // Captive portal detection — redirect all probes to the setup page
        _server.on("/generate_204",        HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/fwlink",              HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/hotspot-detect.html", HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/connecttest.txt",     HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/ncsi.txt",            HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/canonical.html",      HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.on("/success.txt",         HTTP_GET,  [this]() { _redirectToRoot(); });
        _server.onNotFound([this]() { _redirectToRoot(); });

        _server.begin();
        Serial.println("[Server] HTTP server started on port 80.");
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
    // Request handlers
    // -------------------------------------------------------------------------

    void _handleRoot() {
        _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        _server.sendHeader("Pragma",        "no-cache");
        _server.sendHeader("Expires",       "-1");
        _server.send(200, "text/html; charset=utf-8", _buildSetupPage());
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

        if (devName.length() == 0) devName = "SmartDevice";

        SPConfig config;
        memset(&config, 0, sizeof(config));

        strncpy(config.networks[0].ssid,     ssid.c_str(),     sizeof(config.networks[0].ssid)     - 1);
        strncpy(config.networks[0].password, password.c_str(), sizeof(config.networks[0].password) - 1);
        config.networks[0].valid = true;

        String ssid2 = _server.arg("ssid2");
        String pw2   = _server.arg("password2");
        ssid2.trim();

        if (ssid2.length() > 0) {
            strncpy(config.networks[1].ssid,     ssid2.c_str(), sizeof(config.networks[1].ssid)     - 1);
            strncpy(config.networks[1].password, pw2.c_str(),   sizeof(config.networks[1].password) - 1);
            config.networks[1].valid = true;
        }

        strncpy(config.deviceName, devName.c_str(), sizeof(config.deviceName) - 1);
        config.isConfigured = true;

        for (int i = 0; i < _fieldDefCount; i++) {
            String val = _server.arg(_fieldDefs[i].key);
            val.trim();
            if (val.length() == 0) continue;
            strncpy(config.fields[i].key,   _fieldDefs[i].key, SP_FIELD_KEY_LEN - 1);
            strncpy(config.fields[i].value, val.c_str(),        SP_FIELD_VAL_LEN - 1);
            config.fields[i].valid = true;
        }

        if (_onConfigReceived) _onConfigReceived(config);
        _configSaved = true;

        _server.send(200, "text/plain", "OK");
    }

    void _handleNetworks() {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", _networksJson);
    }

    void _handleSuccess() {
        _server.send(200, "text/html; charset=utf-8", _buildSuccessPage());
    }

    void _redirectToRoot() {
        _server.sendHeader("Location", "http://192.168.4.1/");
        _server.send(302, "text/plain", "");
    }

    // -------------------------------------------------------------------------
    // HTML builders
    // -------------------------------------------------------------------------

    String _buildCustomFields() const {
        if (_fieldDefCount == 0) return "";
        String out = "<div class=\"section-divider\"><span>Advanced Settings</span></div>";
        for (int i = 0; i < _fieldDefCount; i++) {
            out += "<div class=\"field\">"
                   "<label>" + String(_fieldDefs[i].label) + "</label>"
                   "<input type=\"text\" name=\"" + String(_fieldDefs[i].key) + "\""
                   " placeholder=\"" + String(_fieldDefs[i].placeholder) + "\""
                   " maxlength=\"63\" autocomplete=\"off\">"
                   "</div>";
        }
        return out;
    }

    String _buildSetupPage() const {
        String page = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>SmartProv Setup</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0a0e17;--surface:#111827;--surface2:#1a2235;
  --border:#1f2d45;--border-focus:#3b82f6;
  --accent:#3b82f6;--accent-hover:#2563eb;
  --green:#10b981;--red:#ef4444;--amber:#f59e0b;
  --text:#f1f5f9;--text-muted:#64748b;--text-sub:#94a3b8;
  --radius:14px;--radius-sm:9px;
}
html{height:100%}
body{
  font-family:-apple-system,BlinkMacSystemFont,'SF Pro Display','Segoe UI',sans-serif;
  background:var(--bg);color:var(--text);min-height:100vh;
  display:flex;align-items:flex-start;justify-content:center;
  padding:16px 16px 40px;
  background-image:radial-gradient(ellipse 80% 50% at 50% -10%,rgba(59,130,246,0.12),transparent);
}
.card{
  width:100%;max-width:420px;
  background:var(--surface);border:1px solid var(--border);
  border-radius:var(--radius);overflow:hidden;
  box-shadow:0 24px 64px rgba(0,0,0,0.5);
}
.brand{
  padding:26px 24px 20px;
  display:flex;align-items:center;gap:14px;
  border-bottom:1px solid var(--border);
  background:linear-gradient(135deg,var(--surface2),var(--surface));
}
.brand-icon{
  width:46px;height:46px;border-radius:12px;
  background:linear-gradient(135deg,#3b82f6,#1d4ed8);
  display:flex;align-items:center;justify-content:center;
  font-size:22px;flex-shrink:0;
  box-shadow:0 4px 16px rgba(59,130,246,0.35);
}
.brand-text h1{font-size:18px;font-weight:700;letter-spacing:-.3px}
.brand-text p{font-size:12px;color:var(--text-muted);margin-top:2px}
.status-bar{
  margin:18px 20px 0;padding:11px 14px;border-radius:var(--radius-sm);
  background:rgba(16,185,129,0.08);border:1px solid rgba(16,185,129,0.2);
  display:flex;align-items:center;gap:10px;font-size:13px;color:#6ee7b7;
}
.pulse{
  width:8px;height:8px;border-radius:50%;background:var(--green);flex-shrink:0;
  animation:pulse 2s ease-in-out infinite;
}
@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.5;transform:scale(.75)}}
.body{padding:20px}
.field{margin-bottom:16px}
label{
  display:block;font-size:11px;font-weight:600;
  letter-spacing:.7px;text-transform:uppercase;
  color:var(--text-muted);margin-bottom:7px;
}
label .hint{font-weight:400;text-transform:none;letter-spacing:0;color:var(--text-muted);opacity:.7;margin-left:6px;}
select,input[type=text],input[type=password]{
  width:100%;background:var(--surface2);border:1px solid var(--border);
  border-radius:var(--radius-sm);padding:12px 14px;font-size:15px;
  color:var(--text);outline:none;-webkit-appearance:none;appearance:none;
  transition:border-color .2s,box-shadow .2s;-webkit-text-size-adjust:100%;
}
select:focus,input:focus{border-color:var(--border-focus);box-shadow:0 0 0 3px rgba(59,130,246,.15);}
input::placeholder{color:var(--text-muted);opacity:.6}
.select-wrap{position:relative}
.select-wrap::after{
  content:'';position:absolute;right:14px;top:50%;transform:translateY(-50%);
  border:5px solid transparent;border-top:6px solid var(--text-muted);
  pointer-events:none;margin-top:3px;
}
.pw-wrap{position:relative}
.pw-wrap input{padding-right:46px}
.pw-toggle{
  position:absolute;right:0;top:0;height:100%;width:46px;
  background:none;border:none;color:var(--text-muted);cursor:pointer;
  display:flex;align-items:center;justify-content:center;font-size:16px;
}
.pw-toggle:focus{outline:none}
.net-meta{display:none;gap:8px;margin-top:8px;flex-wrap:wrap;}
.net-meta.show{display:flex}
.badge{font-size:11px;padding:3px 9px;border-radius:20px;font-weight:500;}
.badge-signal{background:rgba(16,185,129,.1);color:#6ee7b7;border:1px solid rgba(16,185,129,.2)}
.badge-lock{background:rgba(245,158,11,.1);color:#fcd34d;border:1px solid rgba(245,158,11,.2)}
.badge-open{background:rgba(100,116,139,.1);color:#94a3b8;border:1px solid rgba(100,116,139,.2)}
.field-hint{font-size:12px;color:var(--text-muted);margin-top:6px;line-height:1.5}
.section-divider{
  display:flex;align-items:center;gap:12px;
  margin:20px 0 18px;font-size:11px;text-transform:uppercase;
  letter-spacing:.7px;font-weight:600;color:var(--text-muted);
}
.section-divider::before,.section-divider::after{content:'';flex:1;height:1px;background:var(--border);}
.backup-btn{
  width:100%;background:none;border:1px dashed var(--border);
  border-radius:var(--radius-sm);padding:12px;color:var(--text-muted);
  font-size:13px;cursor:pointer;margin-bottom:16px;
  transition:border-color .2s,color .2s,background .2s;
}
.backup-btn:hover,.backup-btn.on{border-color:var(--accent);color:var(--accent);background:rgba(59,130,246,.05);}
#backupSection{display:none}
#backupSection.show{display:block}
.error-box{
  background:rgba(239,68,68,.08);border:1px solid rgba(239,68,68,.25);
  border-radius:var(--radius-sm);padding:11px 14px;font-size:13px;color:#fca5a5;
  margin-bottom:16px;display:none;gap:9px;align-items:flex-start;
  animation:slidein .2s ease;
}
.error-box.show{display:flex}
.connecting-box{
  background:rgba(59,130,246,.08);border:1px solid rgba(59,130,246,.2);
  border-radius:var(--radius-sm);padding:14px;font-size:13px;color:#93c5fd;
  margin-bottom:16px;display:none;align-items:center;gap:12px;
}
.connecting-box.show{display:flex}
@keyframes slidein{from{opacity:0;transform:translateY(-6px)}to{opacity:1;transform:translateY(0)}}
.spinner{
  width:18px;height:18px;border:2px solid rgba(59,130,246,.2);
  border-top-color:var(--accent);border-radius:50%;
  animation:spin .7s linear infinite;flex-shrink:0;
}
@keyframes spin{to{transform:rotate(360deg)}}
.btn{
  width:100%;background:var(--accent);color:#fff;border:none;
  border-radius:var(--radius-sm);padding:15px;font-size:15px;
  font-weight:600;cursor:pointer;letter-spacing:.2px;margin-top:4px;
  transition:background .2s,transform .1s,box-shadow .2s;
  box-shadow:0 4px 16px rgba(59,130,246,.3);
}
.btn:hover{background:var(--accent-hover);box-shadow:0 6px 20px rgba(59,130,246,.4);}
.btn:active{transform:scale(.98)}
.btn:disabled{opacity:.6;cursor:not-allowed;transform:none}
.reset-hint{text-align:center;margin-top:18px;font-size:11px;color:var(--text-muted);line-height:1.6}
.footer{
  text-align:center;padding:14px;border-top:1px solid var(--border);
  font-size:11px;color:var(--text-muted);background:var(--surface2);
}
</style>
</head>
<body>
<div class="card">

  <div class="brand">
    <div class="brand-icon">&#128225;</div>
    <div class="brand-text">
      <h1>SmartProv</h1>
      <p>Zero-Config IoT Setup</p>
    </div>
  </div>

  <div class="status-bar">
    <div class="pulse"></div>
    <span>Setup mode active &mdash; connect your device to WiFi</span>
  </div>

  <div class="body">

    <div class="error-box" id="errBox"><span>&#9888;</span><span id="errMsg"></span></div>
    <div class="connecting-box" id="connectingBox">
      <div class="spinner"></div>
      <span id="connectingMsg">Saving credentials&hellip;</span>
    </div>

    <div class="field">
      <label>WiFi Network</label>
      <div class="select-wrap">
        <select id="ssidSelect" onchange="onNetSelect(this)">
          <option value="">Loading networks&hellip;</option>
        </select>
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
        <button type="button" class="pw-toggle" onclick="togglePw('pw1')" aria-label="Show password">&#128065;</button>
      </div>
      <div class="field-hint" id="pw1Hint" style="display:none">Open network &mdash; no password required.</div>
    </div>

    <button type="button" class="backup-btn" id="backupBtn" onclick="toggleBackup()">
      + Add backup WiFi network (optional)
    </button>

    <div id="backupSection">
      <div class="field">
        <label>Backup SSID</label>
        <input type="text" name="ssid2" id="ssid2" placeholder="Second network name" maxlength="32" autocomplete="off">
      </div>
      <div class="field">
        <label>Backup Password</label>
        <div class="pw-wrap">
          <input type="password" id="pw2" placeholder="Second network password" maxlength="63" autocomplete="off">
          <button type="button" class="pw-toggle" onclick="togglePw('pw2')" aria-label="Show password">&#128065;</button>
        </div>
      </div>
    </div>

    <div class="section-divider"><span>Device Settings</span></div>

    <div class="field">
      <label>Device Name <span class="hint">(optional)</span></label>
      <input type="text" id="devName" placeholder="e.g. Living Room Sensor" maxlength="32" autocomplete="off">
      <div class="field-hint">Used to identify this device in your application.</div>
    </div>

)rawhtml";

        page += _buildCustomFields();

        page += R"rawhtml(
    <button type="button" class="btn" id="submitBtn" onclick="submitForm()">Connect Device</button>
    <p class="reset-hint">To factory reset: hold the reset button for 3 seconds while connected.</p>

  </div>
  <div class="footer">SmartProv v2.1.3 &bull; ESP IoT Provisioning</div>
</div>

<script>
var _nets=[];

function sigLabel(s){return['','Weak','Fair','Good','Excellent'][s]||'';}
function sigBars(s){
  var b='';
  for(var i=1;i<=4;i++){b+='<span style="opacity:'+(i<=s?'1':'.25')+'">&#9646;</span>';}
  return b;
}

window.onload=function(){
  fetch('/networks')
    .then(function(r){return r.json();})
    .then(function(nets){
      _nets=nets;
      var sel=document.getElementById('ssidSelect');
      sel.innerHTML='<option value="">-- Select your network --</option>';
      nets.forEach(function(n,i){
        var o=document.createElement('option');
        o.value=String(i);
        o.textContent=(n.open?'  ':'  ')+n.ssid+' ('+sigLabel(n.signal)+')';
        sel.appendChild(o);
      });
      var m=document.createElement('option');
      m.value='__manual__';m.textContent='  Enter manually...';
      sel.appendChild(m);
    })
    .catch(function(){
      var sel=document.getElementById('ssidSelect');
      sel.innerHTML='<option value="__manual__">Enter network name manually</option>';
      onNetSelect(sel);
    });
};

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
    document.getElementById('metaSignal').innerHTML=sigBars(n.signal)+'&nbsp;'+sigLabel(n.signal);
    var sec=document.getElementById('metaSec');
    sec.className='badge '+(n.open?'badge-open':'badge-lock');
    sec.textContent=n.open?'Open':'Secured';
    hint.style.display=n.open?'block':'none';
  }
}

function togglePw(id){var e=document.getElementById(id);e.type=e.type==='password'?'text':'password';}

function toggleBackup(){
  var s=document.getElementById('backupSection');
  var b=document.getElementById('backupBtn');
  var on=!s.classList.contains('show');
  s.className=on?'show':'';
  b.className='backup-btn'+(on?' on':'');
  b.textContent=on?'- Remove backup network':'+ Add backup WiFi network (optional)';
  if(!on){document.getElementById('ssid2').value='';document.getElementById('pw2').value='';}
}

function showErr(msg){
  var b=document.getElementById('errBox');
  document.getElementById('errMsg').textContent=msg;
  b.className='error-box show';
  b.scrollIntoView({behavior:'smooth',block:'nearest'});
}
function hideErr(){document.getElementById('errBox').className='error-box';}
function setConnecting(msg){
  document.getElementById('connectingMsg').textContent=msg||'Saving credentials...';
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
  if(!ssid){showErr('Please select or enter a WiFi network.');return;}

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
  document.querySelectorAll('input[name]').forEach(function(el){
    if(['ssid','password','deviceName','ssid2','password2'].indexOf(el.name)===-1&&el.name){
      params.set(el.name,el.value.trim());
    }
  });

  setConnecting('Saving credentials\u2026');

  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})
    .then(function(r){
      if(r.ok){
        document.getElementById('connectingMsg').textContent='Saved. Device is restarting\u2026';
        setTimeout(function(){window.location.href='/success';},800);
      } else {
        return r.text().then(function(t){
          document.getElementById('connectingBox').className='connecting-box';
          document.getElementById('submitBtn').disabled=false;
          var m={SSID_EMPTY:'Please select or enter a WiFi network.',SSID_TOO_LONG:'Network name is too long.',PASSWORD_TOO_SHORT:'Password must be at least 8 characters.'};
          showErr(m[t]||'An error occurred. Please try again.');
        });
      }
    })
    .catch(function(){setTimeout(function(){window.location.href='/success';},600);});
}
</script>
</body>
</html>
)rawhtml";
        return page;
    }

    String _buildSuccessPage() const {
        return R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Setup Complete</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0a0e17;--surface:#111827;--border:#1f2d45;--accent:#3b82f6;--text:#f1f5f9;--muted:#64748b}
body{
  font-family:-apple-system,BlinkMacSystemFont,'SF Pro Display','Segoe UI',sans-serif;
  background:var(--bg);color:var(--text);min-height:100vh;
  display:flex;align-items:center;justify-content:center;padding:24px;
  background-image:radial-gradient(ellipse 80% 50% at 50% -10%,rgba(16,185,129,0.1),transparent);
}
.card{
  text-align:center;background:var(--surface);border:1px solid var(--border);
  border-radius:14px;padding:44px 32px;max-width:360px;width:100%;
  box-shadow:0 24px 64px rgba(0,0,0,.5);
}
.icon{
  width:72px;height:72px;border-radius:50%;margin:0 auto 22px;
  background:rgba(16,185,129,.1);border:2px solid rgba(16,185,129,.3);
  display:flex;align-items:center;justify-content:center;font-size:30px;
  animation:pop .5s cubic-bezier(.34,1.56,.64,1);
}
@keyframes pop{0%{transform:scale(0);opacity:0}100%{transform:scale(1);opacity:1}}
h1{font-size:22px;font-weight:700;margin-bottom:10px;letter-spacing:-.3px}
.sub{color:var(--muted);font-size:14px;line-height:1.6;margin-bottom:20px}
.note{
  background:rgba(59,130,246,.07);border:1px solid rgba(59,130,246,.15);
  border-radius:9px;padding:13px 16px;font-size:13px;color:#93c5fd;
  margin-bottom:20px;text-align:left;display:flex;gap:10px;align-items:flex-start;
}
.counter{font-size:13px;color:var(--muted)}
#sec{color:var(--accent);font-weight:600}
.footer{margin-top:24px;font-size:11px;color:var(--muted)}
</style>
</head>
<body>
<div class="card">
  <div class="icon">&#10003;</div>
  <h1>Setup Complete</h1>
  <p class="sub">Your WiFi credentials have been saved.<br>The device is now restarting.</p>
  <div class="note">
    <span>&#128241;</span>
    <span>Reconnect your phone to your regular WiFi network to restore internet access.</span>
  </div>
  <p class="counter">Device restarting in <span id="sec">5</span>s</p>
  <div class="footer">SmartProv v2.1.3 &bull; ESP IoT Provisioning</div>
</div>
<script>
var c=5;var t=setInterval(function(){document.getElementById('sec').textContent=--c;if(c<=0)clearInterval(t);},1000);
</script>
</body>
</html>
)rawhtml";
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    SP_WebServerClass&  _server;
    DNSServer&          _dns;
    SPConfigCallback    _onConfigReceived;
    String              _networksJson;
    bool                _configSaved;
    SPFieldDef          _fieldDefs[SP_MAX_FIELDS];
    int                 _fieldDefCount;
};

#endif // SP_SERVER_H
