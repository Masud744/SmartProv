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
        _server.send(200, "text/html", _buildHTML());
    }

    void _handleSave() {
        const String ssid      = _server.arg("ssid");
        const String password  = _server.arg("password");
        const String ssid2     = _server.arg("ssid2");
        const String password2 = _server.arg("password2");
        String       devName   = _server.arg("deviceName");

        if (ssid.length() == 0) {
            _server.send(400, "text/plain", "SSID cannot be empty.");
            return;
        }
        if (ssid.length() > 32) {
            _server.send(400, "text/plain", "SSID exceeds maximum length (32 characters).");
            return;
        }
        if (password.length() > 0 && password.length() < 8) {
            _server.send(400, "text/plain", "Password must be at least 8 characters.");
            return;
        }

        if (devName.length() == 0) devName = "SmartDevice";

        SPConfig config;
        memset(&config, 0, sizeof(config));

        strncpy(config.networks[0].ssid,     ssid.c_str(),     63);
        strncpy(config.networks[0].password, password.c_str(), 63);
        config.networks[0].valid = true;

        if (ssid2.length() > 0) {
            strncpy(config.networks[1].ssid,     ssid2.c_str(),     63);
            strncpy(config.networks[1].password, password2.c_str(), 63);
            config.networks[1].valid = true;
        }

        strncpy(config.deviceName, devName.c_str(), 31);
        config.isConfigured = true;

        for (int i = 0; i < _fieldDefCount; i++) {
            const String val = _server.arg(_fieldDefs[i].key);
            if (val.length() == 0) continue;
            strncpy(config.fields[i].key,   _fieldDefs[i].key, SP_FIELD_KEY_LEN - 1);
            strncpy(config.fields[i].value, val.c_str(),        SP_FIELD_VAL_LEN - 1);
            config.fields[i].valid = true;
        }

        if (_onConfigReceived) _onConfigReceived(config);
        _configSaved = true;

        _server.sendHeader("Location", "/success");
        _server.send(302, "text/plain", "");
    }

    void _handleNetworks() {
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", _networksJson);
    }

    void _handleSuccess() {
        _server.send(200, "text/html", _successPage());
    }

    void _redirectToRoot() {
        _server.sendHeader("Location", "http://192.168.4.1/");
        _server.send(302, "text/plain", "");
    }

    // -------------------------------------------------------------------------
    // HTML generation
    // -------------------------------------------------------------------------

    String _buildCustomFieldsHTML() const {
        String html;
        for (int i = 0; i < _fieldDefCount; i++) {
            html += "<div class=\"field\">"
                    "<label>" + String(_fieldDefs[i].label) + "</label>"
                    "<input type=\"text\" name=\"" + String(_fieldDefs[i].key) + "\""
                    " placeholder=\"" + String(_fieldDefs[i].placeholder) + "\""
                    " maxlength=\"" + String(SP_FIELD_VAL_LEN - 1) + "\">"
                    "</div>";
        }
        return html;
    }

    String _buildHTML() const {
        const String customFields = _buildCustomFieldsHTML();

        String html = F(
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1\">"
"<title>Device Setup</title>"
"<style>"
"*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}"
":root{"
"--bg:#0d1117;--surface:#161b22;--border:#30363d;"
"--accent:#58a6ff;--green:#3fb950;--text:#e6edf3;"
"--muted:#8b949e;--danger:#f85149;--radius:12px}"
"body{"
"font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
"background:var(--bg);color:var(--text);"
"min-height:100vh;display:flex;align-items:center;"
"justify-content:center;padding:20px;"
"background-image:linear-gradient(rgba(88,166,255,.03) 1px,transparent 1px),"
"linear-gradient(90deg,rgba(88,166,255,.03) 1px,transparent 1px);"
"background-size:40px 40px}"
".card{"
"background:var(--surface);border:1px solid var(--border);"
"border-radius:var(--radius);padding:32px 24px;"
"width:100%;max-width:420px;"
"box-shadow:0 0 30px rgba(88,166,255,.1),0 8px 32px rgba(0,0,0,.4)}"
".header{text-align:center;margin-bottom:24px}"
".logo{"
"width:52px;height:52px;"
"background:linear-gradient(135deg,var(--accent),#1f6feb);"
"border-radius:14px;display:flex;align-items:center;"
"justify-content:center;margin:0 auto 12px;font-size:24px}"
"h1{font-size:20px;font-weight:600}"
".subtitle{font-size:13px;color:var(--muted);margin-top:4px}"
".status-bar{"
"display:flex;align-items:center;gap:8px;"
"background:rgba(88,166,255,.08);border:1px solid rgba(88,166,255,.2);"
"border-radius:8px;padding:10px 14px;margin-bottom:20px;"
"font-size:12px;color:var(--accent)}"
".dot{"
"width:8px;height:8px;background:var(--green);"
"border-radius:50%;flex-shrink:0;"
"animation:pulse 2s infinite}"
"@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.5;transform:scale(.8)}}"
".field{margin-bottom:16px}"
"label{"
"display:block;font-size:11px;font-weight:600;"
"color:var(--muted);text-transform:uppercase;"
"letter-spacing:.6px;margin-bottom:7px}"
"input,select{"
"width:100%;background:var(--bg);border:1px solid var(--border);"
"border-radius:8px;padding:11px 13px;font-size:15px;"
"color:var(--text);outline:none;"
"transition:border-color .2s,box-shadow .2s;"
"appearance:none;-webkit-appearance:none}"
"input:focus,select:focus{"
"border-color:var(--accent);"
"box-shadow:0 0 0 3px rgba(88,166,255,.15)}"
"input::placeholder{color:var(--muted)}"
".select-wrap{position:relative}"
".select-wrap::after{"
"content:'\\25BE';position:absolute;right:13px;top:50%;"
"transform:translateY(-50%);color:var(--muted);"
"pointer-events:none;font-size:12px}"
".input-wrap{position:relative}"
".toggle-pw{"
"position:absolute;right:11px;top:50%;"
"transform:translateY(-50%);background:none;"
"border:none;color:var(--muted);cursor:pointer;"
"font-size:15px;padding:4px;line-height:1}"
".input-wrap input{padding-right:42px}"
".divider{"
"display:flex;align-items:center;gap:10px;"
"margin:18px 0;color:var(--muted);font-size:11px}"
".divider::before,.divider::after{"
"content:'';flex:1;height:1px;background:var(--border)}"
".backup-toggle{text-align:center;margin-bottom:16px}"
".backup-toggle button{"
"background:none;border:1px dashed var(--border);"
"color:var(--muted);font-size:12px;cursor:pointer;"
"padding:8px 16px;border-radius:6px;width:100%;"
"transition:border-color .2s,color .2s}"
".backup-toggle button:hover{border-color:var(--accent);color:var(--accent)}"
"#backupNetwork{display:none}"
".backup-label{"
"font-size:11px;color:var(--accent);text-transform:uppercase;"
"letter-spacing:.6px;margin-bottom:12px;"
"display:flex;align-items:center;gap:6px}"
".btn{"
"width:100%;background:var(--accent);color:#0d1117;"
"border:none;border-radius:8px;padding:14px;"
"font-size:15px;font-weight:600;cursor:pointer;"
"transition:opacity .2s,transform .1s;margin-top:8px;"
"position:relative;overflow:hidden}"
".btn:hover{opacity:.9}"
".btn:active{transform:scale(.98)}"
".btn.loading::after{"
"content:'';position:absolute;inset:0;"
"background:linear-gradient(90deg,transparent,rgba(255,255,255,.2),transparent);"
"animation:shimmer 1.2s infinite}"
"@keyframes shimmer{0%{transform:translateX(-100%)}100%{transform:translateX(100%)}}"
".error{"
"background:rgba(248,81,73,.1);border:1px solid rgba(248,81,73,.3);"
"border-radius:8px;padding:10px 14px;font-size:13px;"
"color:var(--danger);margin-bottom:14px;display:none}"
".footer{text-align:center;margin-top:18px;font-size:11px;color:var(--muted)}"
"</style>"
"</head>"
"<body>"
"<div class=\"card\">"
"<div class=\"header\">"
"<div class=\"logo\">&#x1F4E1;</div>"
"<h1>Device Setup</h1>"
"<p class=\"subtitle\">Connect your device to WiFi</p>"
"</div>"
"<div class=\"status-bar\">"
"<div class=\"dot\"></div>"
"<span>Setup mode active &mdash; fill in your WiFi details</span>"
"</div>"
"<div class=\"error\" id=\"errorMsg\"></div>"
"<form id=\"setupForm\">"
"<div class=\"field\">"
"<label>WiFi Network</label>"
"<div class=\"select-wrap\">"
"<select id=\"ssidSelect\" onchange=\"onNetworkSelect(this)\">"
"<option value=\"\">Loading networks...</option>"
"</select>"
"</div>"
"</div>"
"<div class=\"field\" id=\"manualSsidField\" style=\"display:none\">"
"<label>Network Name (SSID)</label>"
"<input type=\"text\" id=\"manualSsid\" placeholder=\"Enter WiFi name\" maxlength=\"32\">"
"</div>"
"<input type=\"hidden\" id=\"ssidHidden\" name=\"ssid\">"
"<div class=\"field\">"
"<label>Password</label>"
"<div class=\"input-wrap\">"
"<input type=\"password\" name=\"password\" id=\"password\""
" placeholder=\"WiFi password\" maxlength=\"63\">"
"<button type=\"button\" class=\"toggle-pw\" onclick=\"togglePw('password')\">&#x1F441;</button>"
"</div>"
"</div>"
"<div class=\"backup-toggle\">"
"<button type=\"button\" onclick=\"toggleBackup()\">"
"+ Add backup WiFi network (optional)"
"</button>"
"</div>"
"<div id=\"backupNetwork\">"
"<div class=\"backup-label\">Backup Network</div>"
"<div class=\"field\">"
"<label>Backup SSID</label>"
"<input type=\"text\" name=\"ssid2\" placeholder=\"Second network name\" maxlength=\"32\">"
"</div>"
"<div class=\"field\">"
"<label>Backup Password</label>"
"<div class=\"input-wrap\">"
"<input type=\"password\" name=\"password2\" id=\"password2\""
" placeholder=\"Second network password\" maxlength=\"63\">"
"<button type=\"button\" class=\"toggle-pw\" onclick=\"togglePw('password2')\">&#x1F441;</button>"
"</div>"
"</div>"
"</div>"
"<div class=\"divider\">device settings</div>"
"<div class=\"field\">"
"<label>Device Name <span style=\"color:var(--muted);font-weight:400\">(optional)</span></label>"
"<input type=\"text\" name=\"deviceName\" placeholder=\"e.g. Living Room Sensor\" maxlength=\"32\">"
"</div>"
        );

        html += customFields;

        html += F(
"<button type=\"button\" class=\"btn\" id=\"submitBtn\" onclick=\"submitForm()\">"
"Connect Device"
"</button>"
"</form>"
"<div class=\"footer\">SmartProv v2.0 &bull; ESP IoT Provisioning</div>"
"</div>"
"<script>"
"window.onload=function(){"
"fetch('/networks')"
".then(function(r){return r.json();})"
".then(function(nets){"
"var sel=document.getElementById('ssidSelect');"
"sel.innerHTML='<option value=\"\">-- Select your network --</option>';"
"nets.forEach(function(n){"
"var o=document.createElement('option');"
"var bars=['\\u2581','\\u2581\\u2582','\\u2581\\u2582\\u2583','\\u2581\\u2582\\u2583\\u2584'][Math.min(n.signal-1,3)]||'\\u2581';"
"var lock=n.open?'\\uD83D\\uDD13':'\\uD83D\\uDD12';"
"o.textContent=bars+' '+lock+' '+n.ssid+' ('+n.rssi+' dBm)';"
"o.value=n.ssid;"
"sel.appendChild(o);});"
"var m=document.createElement('option');"
"m.value='__manual__';m.textContent='Enter manually...';"
"sel.appendChild(m);})"
".catch(function(){"
"var sel=document.getElementById('ssidSelect');"
"sel.innerHTML='<option value=\"__manual__\">Enter name manually</option>';"
"onNetworkSelect(sel);});};"
"function onNetworkSelect(sel){"
"var mf=document.getElementById('manualSsidField');"
"var sh=document.getElementById('ssidHidden');"
"if(sel.value==='__manual__'){mf.style.display='block';sh.value='';}"
"else{mf.style.display='none';sh.value=sel.value;}}"
"function togglePw(id){"
"var el=document.getElementById(id);"
"el.type=el.type==='password'?'text':'password';}"
"function toggleBackup(){"
"var el=document.getElementById('backupNetwork');"
"el.style.display=el.style.display==='none'?'block':'none';}"
"function submitForm(){"
"var err=document.getElementById('errorMsg');"
"err.style.display='none';"
"var selVal=document.getElementById('ssidSelect').value;"
"if(selVal==='__manual__'){"
"document.getElementById('ssidHidden').value="
"document.getElementById('manualSsid').value.trim();}"
"var ssid=document.getElementById('ssidHidden').value.trim();"
"var pw=document.getElementById('password').value;"
"if(!ssid){showError('Please select or enter a WiFi network.');return;}"
"if(pw.length>0&&pw.length<8){"
"showError('Password must be at least 8 characters.');return;}"
"var btn=document.getElementById('submitBtn');"
"btn.textContent='Connecting...';btn.classList.add('loading');btn.disabled=true;"
"var params=new URLSearchParams(new FormData(document.getElementById('setupForm')));"
"params.set('ssid',ssid);"
"fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})"
".then(function(){window.location.href='/success';})"
".catch(function(){window.location.href='/success';})"
".finally(function(){btn.classList.remove('loading');btn.disabled=false;btn.textContent='Connect Device';});}"
"function showError(msg){"
"var d=document.getElementById('errorMsg');"
"d.textContent=msg;d.style.display='block';"
"d.scrollIntoView({behavior:'smooth',block:'center'});}"
"</script>"
"</body>"
"</html>"
        );

        return html;
    }

    String _successPage() const {
        return String(F(
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Setup Complete</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
"background:#0d1117;color:#e6edf3;min-height:100vh;"
"display:flex;align-items:center;justify-content:center;padding:20px}"
".card{text-align:center;background:#161b22;border:1px solid #30363d;"
"border-radius:12px;padding:40px 28px;max-width:360px;width:100%}"
".icon{font-size:56px;margin-bottom:16px;animation:pop .6s ease}"
"@keyframes pop{0%{transform:scale(0);opacity:0}70%{transform:scale(1.15)}100%{transform:scale(1);opacity:1}}"
"h1{font-size:22px;font-weight:600;margin-bottom:8px}"
"p{color:#8b949e;font-size:14px;line-height:1.6;margin-bottom:6px}"
".note{margin-top:18px;background:rgba(63,185,80,.1);"
"border:1px solid rgba(63,185,80,.3);border-radius:8px;"
"padding:12px;font-size:13px;color:#3fb950}"
".counter{font-size:13px;color:#8b949e;margin-top:14px}"
"#count{color:#58a6ff;font-weight:600}"
"</style>"
"</head>"
"<body>"
"<div class=\"card\">"
"<div class=\"icon\">&#x2705;</div>"
"<h1>Setup Complete</h1>"
"<p>WiFi credentials have been saved.</p>"
"<p>The device is restarting now.</p>"
"<div class=\"note\">Reconnect your phone to your regular WiFi network.</div>"
"<p class=\"counter\">Restarting in <span id=\"count\">5</span>s</p>"
"</div>"
"<script>"
"var c=5;"
"var t=setInterval(function(){"
"document.getElementById('count').textContent=--c;"
"if(c<=0)clearInterval(t);},1000);"
"</script>"
"</body>"
"</html>"
        ));
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
