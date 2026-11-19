#ifndef SMARTPROV_H
#define SMARTPROV_H

#define SP_VERSION "3.0.0"

#if !defined(ESP32) && !defined(ESP8266)
    #error "SmartProv requires ESP32 or ESP8266."
#endif

#include "SP_Storage.h"
#include "SP_WiFi.h"
#include "SP_Server.h"

#ifdef ESP32
    #include <WebServer.h>
    #include <DNSServer.h>
    #include <ESPmDNS.h>
    #define _SP_WebServer WebServer
#else
    #include <ESP8266WebServer.h>
    #include <DNSServer.h>
    #include <ESP8266mDNS.h>
    #define _SP_WebServer ESP8266WebServer
#endif

#ifndef SP_AP_PREFIX
    #define SP_AP_PREFIX        "SmartProv"
#endif
#ifndef SP_RESET_PIN
    #define SP_RESET_PIN        0
#endif
#ifndef SP_LED_PIN
    #define SP_LED_PIN          2
#endif
#ifndef SP_RESET_HOLD_MS
    #define SP_RESET_HOLD_MS    3000
#endif
#ifndef SP_RESTART_DELAY_MS
    #define SP_RESTART_DELAY_MS 3000
#endif

class SmartProv {
public:

    SmartProv()
        : _server(_webServer, _dnsServer),
          _appState(SP_APP_INIT),
          _currentNetIdx(0),
          _resetPin(SP_RESET_PIN),
          _ledPin(SP_LED_PIN),
          _saveTimestamp(0),
          _resetPressed(false),
          _resetPressStart(0),
          _lastLedToggle(0),
          _ledState(false),
          _onConnectedCb(nullptr),
          _onLoopCb(nullptr),
          _onDisconnectedCb(nullptr),
          _onSetupModeCb(nullptr),
          _onFactoryResetCb(nullptr),
          _mdnsEnabled(false)
    {
        memset(_wrongPasswordNets, 0, sizeof(_wrongPasswordNets));
    }

    void begin(uint8_t resetPin = SP_RESET_PIN, uint8_t ledPin = SP_LED_PIN) {
        _resetPin = resetPin;
        _ledPin   = ledPin;

        Serial.println(F("\n============================="));
        Serial.printf("  SmartProv v%s\n", SP_VERSION);
        Serial.println(F("============================="));

        pinMode(_ledPin,   OUTPUT);
        pinMode(_resetPin, INPUT_PULLUP);
        _setLed(false);
        memset(_wrongPasswordNets, 0, sizeof(_wrongPasswordNets));

        _storage.init();
        _config = _storage.load();

        Serial.printf("[SmartProv] Configured: %s\n",
                      _config.isConfigured ? "YES" : "NO");

        if (_config.isConfigured) {
            const int idx = _findNextValidNetwork(-1);
            if (idx >= 0) {
                _currentNetIdx = idx;
                Serial.printf("[SmartProv] Trying network: %s\n",
                              _config.networks[_currentNetIdx].ssid);
                _transitionTo(SP_APP_CONNECTING);
            } else {
                Serial.println(F("[SmartProv] No valid networks in storage."));
                _transitionTo(SP_APP_SETUP_MODE);
            }
        } else {
            _transitionTo(SP_APP_SETUP_MODE);
        }
    }

    void update() {
        _handleResetButton();
        _updateLED();

        switch (_appState) {

            case SP_APP_CONNECTING: {
                const SPWiFiState ws = _wifi.update();

                if (ws == SP_WIFI_CONNECTED) {
                    _transitionTo(SP_APP_CONNECTED);
                    break;
                }

                if (ws == SP_WIFI_FAILED) {
                    if (_wifi.isWrongPassword()) {
                        Serial.printf("[SmartProv] Wrong password for: %s\n",
                                      _config.networks[_currentNetIdx].ssid);
                        _wrongPasswordNets[_currentNetIdx] = true;

                        const int next = _findNextConnectableNetwork(_currentNetIdx);
                        if (next >= 0) {
                            _currentNetIdx = next;
                            Serial.printf("[SmartProv] Trying next network: %s\n",
                                          _config.networks[next].ssid);
                            _wifi.beginSTA(_config.networks[next].ssid,
                                           _config.networks[next].password);
                        } else {
                            const int first = _findNextConnectableNetwork(-1);
                            if (first >= 0 && !_wifi.hasExceededReconnectLimit()) {
                                _currentNetIdx = first;
                                Serial.printf("[SmartProv] Retrying network: %s\n",
                                              _config.networks[_currentNetIdx].ssid);
                                _wifi.beginSTA(_config.networks[_currentNetIdx].ssid,
                                               _config.networks[_currentNetIdx].password);
                            } else {
                                Serial.println(F("[SmartProv] All networks failed (wrong password / unreachable). Entering setup mode."));
                                _wifi.resetReconnectCounter();
                                _transitionTo(SP_APP_SETUP_MODE);
                            }
                        }
                        break;
                    }

                    if (_wifi.hasExceededReconnectLimit()) {
                        Serial.println(F("[SmartProv] Reconnect limit reached. Entering setup mode."));
                        _wifi.resetReconnectCounter();
                        _transitionTo(SP_APP_SETUP_MODE);
                        break;
                    }

                    const int next = _findNextConnectableNetwork(_currentNetIdx);
                    if (next >= 0) {
                        _currentNetIdx = next;
                        Serial.printf("[SmartProv] Trying next network: %s\n",
                                      _config.networks[next].ssid);
                        _wifi.beginSTA(_config.networks[next].ssid,
                                       _config.networks[next].password);
                    } else {
                        _currentNetIdx = _findNextConnectableNetwork(-1);
                        if (_currentNetIdx < 0 || _wifi.hasExceededReconnectLimit()) {
                            Serial.println(F("[SmartProv] All networks exhausted. Entering setup mode."));
                            _wifi.resetReconnectCounter();
                            _transitionTo(SP_APP_SETUP_MODE);
                        } else {
                            Serial.printf("[SmartProv] Retrying from network: %s\n",
                                          _config.networks[_currentNetIdx].ssid);
                            _wifi.beginSTA(_config.networks[_currentNetIdx].ssid,
                                           _config.networks[_currentNetIdx].password);
                        }
                    }
                }
                break;
            }

            case SP_APP_CONNECTED: {
                if (_onLoopCb) _onLoopCb();

#ifdef ESP8266
                if (_mdnsEnabled) MDNS.update();
#endif

                if (!_wifi.isConnected()) {
                    Serial.println(F("[SmartProv] Connection lost. Reconnecting..."));
                    if (_onDisconnectedCb) _onDisconnectedCb();
                    _transitionTo(SP_APP_CONNECTING);
                }
                break;
            }

            case SP_APP_SETUP_MODE: {
                _server.update();
                if (_server.isConfigSaved()) _transitionTo(SP_APP_SAVING);
                break;
            }

            case SP_APP_SAVING: {
                _server.update();
                if (millis() - _saveTimestamp > SP_RESTART_DELAY_MS) {
                    _transitionTo(SP_APP_RESTARTING);
                }
                break;
            }

            case SP_APP_RESTARTING: {
                _server.stop();
                Serial.println(F("[SmartProv] Restarting device."));
                delay(300);
                ESP.restart();
                break;
            }

            default:
                break;
        }
    }

    bool    isConnected()   const { return _appState == SP_APP_CONNECTED && _wifi.isConnected(); }
    bool    isSetupMode()   const { return _appState == SP_APP_SETUP_MODE; }
    String  getIP()         const { return _wifi.getIP(); }
    String  getSSID()       const { return (_currentNetIdx >= 0) ? String(_config.networks[_currentNetIdx].ssid) : ""; }
    String  getDeviceName() const { return String(_config.deviceName); }
    String  getAPName()     const { return _apName; }
    String  getMACAddress() const { return _wifi.getMACAddress(); }
    int32_t getRSSI()       const { return _wifi.getRSSI(); }

    String getField(const char* key) const {
        return _storage.getField(_config, key);
    }

    void addField(const char* key, const char* label, const char* placeholder = "") {
        _server.addFieldDef(key, label, placeholder);
    }

    void setAPPassword(const char* password) { _apPassword = password; }

    void enableMDNS(const char* hostname = "smartprov") {
        _mdnsEnabled = true;
        _mdnsHostname = hostname;
    }

    void onConnected(void (*cb)()) { _onConnectedCb = cb; }
    void onLoop(void (*cb)()) { _onLoopCb = cb; }
    void onDisconnected(void (*cb)()) { _onDisconnectedCb = cb; }
    void onSetupMode(void (*cb)()) { _onSetupModeCb = cb; }
    void onFactoryReset(void (*cb)()) { _onFactoryResetCb = cb; }

    void resetCredentials() {
        if (_onFactoryResetCb) _onFactoryResetCb();
        _storage.clear();
        Serial.println(F("[SmartProv] Credentials erased. Restarting."));
        delay(300);
        ESP.restart();
    }

    SP_Storage& getStorage() { return _storage; }

private:
    enum SPAppState {
        SP_APP_INIT,
        SP_APP_CONNECTING,
        SP_APP_CONNECTED,
        SP_APP_SETUP_MODE,
        SP_APP_SAVING,
        SP_APP_RESTARTING
    };

    int _findNextValidNetwork(int after) const {
        for (int i = after + 1; i < SP_MAX_NETWORKS; i++) {
            if (_config.networks[i].valid && strlen(_config.networks[i].ssid) > 0) {
                return i;
            }
        }
        return -1;
    }

    int _findNextConnectableNetwork(int after) const {
        for (int i = after + 1; i < SP_MAX_NETWORKS; i++) {
            if (_config.networks[i].valid && strlen(_config.networks[i].ssid) > 0 && !_wrongPasswordNets[i]) {
                return i;
            }
        }
        return -1;
    }

    void _transitionTo(SPAppState next) {
        Serial.printf("[SmartProv] State: %d -> %d\n",
                      static_cast<int>(_appState), static_cast<int>(next));
        _appState = next;

        switch (next) {

            case SP_APP_CONNECTING:
                _wifi.beginSTA(
                    _config.networks[_currentNetIdx].ssid,
                    _config.networks[_currentNetIdx].password
                );
                break;

            case SP_APP_CONNECTED:
                Serial.printf("[SmartProv] Connected — IP: %s | RSSI: %d dBm\n",
                              getIP().c_str(), getRSSI());
                _setLed(true);
                if (_onConnectedCb) {
                    _onConnectedCb();
                }
                if (_mdnsEnabled) {
                    MDNS.begin(_mdnsHostname.c_str());
                    Serial.printf("[SmartProv] mDNS: %s.local\n", _mdnsHostname.c_str());
                }
                break;

            case SP_APP_SETUP_MODE: {
                _instance = this;
                memset(_wrongPasswordNets, 0, sizeof(_wrongPasswordNets));
                _apName = _wifi.generateAPName(SP_AP_PREFIX);
                _wifi.startAP(_apName, _apPassword.c_str());
                const String nets = _wifi.scanNetworks();
                _server.setRescanCallback([]() -> String {
                    if (_instance) return _instance->_wifi.scanNetworks();
                    return "[]";
                });
                _server.begin(WiFi.softAPIP(), nets, [](SPConfig cfg) {
                    if (_instance) _instance->_onConfigReceived(cfg);
                });
                Serial.printf("[SmartProv] AP: %s\n", _apName.c_str());
                if (_onSetupModeCb) _onSetupModeCb();
                break;
            }

            case SP_APP_SAVING:
                _saveTimestamp = millis();
                break;

            default:
                break;
        }
    }

    void _onConfigReceived(const SPConfig& cfg) {
        Serial.printf("[SmartProv] Config received for SSID: %s\n",
                      cfg.networks[0].ssid);
        _storage.save(cfg);
        _config = cfg;
    }

    // -----------------------------------------------------------------------
    // Reset button
    // -----------------------------------------------------------------------

    void _handleResetButton() {
        if (Serial.available()) {
            const char c = Serial.read();
            if (c == 'R' || c == 'r') {
                Serial.println(F("[SmartProv] Factory reset triggered via Serial command."));
                resetCredentials();
                return;
            }
        }

        // Factory reset is permitted while CONNECTED or CONNECTING.
        if (_appState != SP_APP_CONNECTED && _appState != SP_APP_CONNECTING) {
            _resetPressed = false;
            return;
        }

        const bool pressed = (digitalRead(_resetPin) == LOW);

        if (pressed && !_resetPressed) {
            _resetPressed    = true;
            _resetPressStart = millis();
        }
        if (!pressed) {
            _resetPressed = false;
        }
        if (_resetPressed && (millis() - _resetPressStart > SP_RESET_HOLD_MS)) {
            Serial.println(F("[SmartProv] Factory reset triggered via hardware button."));
            if (_onFactoryResetCb) _onFactoryResetCb();
            _storage.clear();
            delay(300);
            ESP.restart();
        }
    }

    void _updateLED() {
        unsigned long blinkInterval = 0;

        switch (_appState) {
            case SP_APP_SETUP_MODE:
            case SP_APP_SAVING:
                blinkInterval = 200;
                break;
            case SP_APP_CONNECTING:
                blinkInterval = 1000;
                break;
            case SP_APP_CONNECTED:
                _setLed(true);
                return;
            default:
                _setLed(false);
                return;
        }

        if (millis() - _lastLedToggle >= blinkInterval) {
            _lastLedToggle = millis();
            _ledState      = !_ledState;
            _setLed(_ledState);
        }
    }

    void _setLed(bool on) const {
#ifdef ESP8266
        digitalWrite(_ledPin, on ? LOW : HIGH);
#else
        digitalWrite(_ledPin, on ? HIGH : LOW);
#endif
    }

    SP_Storage      _storage;
    SP_WiFi         _wifi;
    _SP_WebServer   _webServer{80};
    DNSServer       _dnsServer;
    SP_Server       _server;

    SPAppState      _appState;
    SPConfig        _config;
    String          _apName;
    int             _currentNetIdx;

    uint8_t         _resetPin;
    uint8_t         _ledPin;
    unsigned long   _saveTimestamp;
    bool            _resetPressed;
    unsigned long   _resetPressStart;
    unsigned long   _lastLedToggle;
    bool            _ledState;

    void (*_onConnectedCb)();
    void (*_onLoopCb)();
    void (*_onDisconnectedCb)();
    void (*_onSetupModeCb)();
    void (*_onFactoryResetCb)();

    bool            _mdnsEnabled;
    String          _mdnsHostname;
    String          _apPassword;
    bool            _wrongPasswordNets[SP_MAX_NETWORKS];

    // Pointer for static WebServer lambda callbacks
    inline static SmartProv* _instance = nullptr;
};

#endif // SMARTPROV_H
