/**
 * SmartProv.h
 * SmartProv Main Library Header — v3.0.0
 *
 * Single-include entry point for the SmartProv Wi-Fi provisioning library.
 * Supported platforms: ESP32, ESP8266 / NodeMCU.
 *
 * Minimal usage:
 *
 *   #include <SmartProv.h>
 *   SmartProv prov;
 *
 *   void setup() { Serial.begin(115200); prov.begin(); }
 *   void loop()  { prov.update(); }
 *
 * Provisioning flow:
 *   1. On boot, if valid credentials exist in flash, SmartProv attempts
 *      automatic STA connection using each saved network in order.
 *   2. If all networks fail or no credentials are stored, the device
 *      starts an Access Point and serves a captive portal setup page.
 *   3. The user connects to the AP, submits credentials via the web form.
 *   4. Credentials are persisted to flash, the device restarts, and
 *      reconnects automatically on the next boot.
 *
 * Customisation macros (define before including SmartProv.h):
 *   SP_AP_PREFIX        - AP name prefix string   (default: "SmartProv")
 *   SP_RESET_PIN        - Factory reset GPIO       (default: 0)
 *   SP_LED_PIN          - Status LED GPIO          (default: 2)
 *   SP_RESET_HOLD_MS    - Reset hold duration ms   (default: 3000)
 *   SP_RESTART_DELAY_MS - Post-save restart delay  (default: 3000)
 */

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

// ---------------------------------------------------------------------------
// SmartProv
// ---------------------------------------------------------------------------

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
          _otaEnabled(false),
          _mdnsEnabled(false)
    {
        memset(_wrongPasswordNets, 0, sizeof(_wrongPasswordNets));
    }

    /**
     * Initialises SmartProv. Call once from setup().
     * Loads persisted configuration and transitions to the appropriate
     * initial state (connecting or setup mode).
     *
     * @param resetPin GPIO to monitor for factory reset (active-low, INPUT_PULLUP)
     * @param ledPin   GPIO used for status LED indication
     */
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

    /**
     * Drives the SmartProv state machine. Must be called every loop() iteration.
     */
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

    // -----------------------------------------------------------------------
    // Status accessors
    // -----------------------------------------------------------------------

    bool    isConnected()   const { return _appState == SP_APP_CONNECTED && _wifi.isConnected(); }
    bool    isSetupMode()   const { return _appState == SP_APP_SETUP_MODE; }
    String  getIP()         const { return _wifi.getIP(); }
    String  getSSID()       const { return (_currentNetIdx >= 0) ? String(_config.networks[_currentNetIdx].ssid) : ""; }
    String  getDeviceName() const { return String(_config.deviceName); }
    String  getAPName()     const { return _apName; }
    String  getMACAddress() const { return _wifi.getMACAddress(); }
    int32_t getRSSI()       const { return _wifi.getRSSI(); }

    /**
     * Retrieve a saved custom field value by key.
     * Returns an empty String if the key was not set during provisioning.
     */
    String getField(const char* key) const {
        return _storage.getField(_config, key);
    }

    // -----------------------------------------------------------------------
    // Configuration API
    // -----------------------------------------------------------------------

    /**
     * Register a custom field to appear in the provisioning form.
     * Must be called before begin().
     *
     * @param key         Storage key (max 15 characters)
     * @param label       Form label displayed to the user
     * @param placeholder Input placeholder text
     */
    void addField(const char* key, const char* label, const char* placeholder = "") {
        _server.addFieldDef(key, label, placeholder);
    }

    /**
     * Set a password for the provisioning AP. Call before begin().
     * If not set (or empty), the AP runs open.
     */
    void setAPPassword(const char* password) { _apPassword = password; }

    /**
     * Enable OTA firmware updates via the captive portal.
     * Call before begin().
     */
    void enableOTA(const char* password = "") {
        _otaEnabled = true;
        _server.enableOTA();
    }

    /**
     * Enable mDNS so the device is reachable at <hostname>.local
     * once connected to WiFi. Call before begin().
     */
    void enableMDNS(const char* hostname = "smartprov") {
        _mdnsEnabled = true;
        _mdnsHostname = hostname;
    }

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------

    /** Called every time a successful Wi-Fi connection is established. */
    void onConnected(void (*cb)()) { _onConnectedCb = cb; }

    /** Called every update() cycle while the device is connected. */
    void onLoop(void (*cb)()) { _onLoopCb = cb; }

    /** Called when WiFi connection is lost, before reconnect begins. */
    void onDisconnected(void (*cb)()) { _onDisconnectedCb = cb; }

    /** Called when the device enters setup (AP) mode. */
    void onSetupMode(void (*cb)()) { _onSetupModeCb = cb; }

    /** Called just before credentials are erased (factory reset). */
    void onFactoryReset(void (*cb)()) { _onFactoryResetCb = cb; }

    // -----------------------------------------------------------------------
    // Manual control
    // -----------------------------------------------------------------------

    /** Erases all stored credentials and restarts into setup mode. */
    void resetCredentials() {
        if (_onFactoryResetCb) _onFactoryResetCb();
        _storage.clear();
        Serial.println(F("[SmartProv] Credentials erased. Restarting."));
        delay(300);
        ESP.restart();
    }

    SP_Storage& getStorage() { return _storage; }

private:

    // -----------------------------------------------------------------------
    // State machine
    // -----------------------------------------------------------------------

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

    // -----------------------------------------------------------------------
    // LED control
    // -----------------------------------------------------------------------

    /**
     * LED logic is centralised here to avoid macro leakage into global scope.
     * ESP8266 built-in LED is active-low; ESP32 is active-high.
     * Blink rates encode state: fast = setup mode, slow = connecting, solid = connected.
     */
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
        // ESP8266 built-in LED is active-low
        digitalWrite(_ledPin, on ? LOW : HIGH);
#else
        digitalWrite(_ledPin, on ? HIGH : LOW);
#endif
    }

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

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

    bool            _otaEnabled;
    bool            _mdnsEnabled;
    String          _mdnsHostname;
    String          _apPassword;
    bool            _wrongPasswordNets[SP_MAX_NETWORKS];

    // Singleton pointer used only to bridge the lambda capture limitation
    // of the ESP Arduino SDK (std::function not available on all builds).
    // Deliberately not exposed in the public API.
    inline static SmartProv* _instance = nullptr;
};

#endif // SMARTPROV_H
