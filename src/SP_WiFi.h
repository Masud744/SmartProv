/**
 * SP_WiFi.h
 * SmartProv Wi-Fi Module — v2.1.3
 *
 * Manages all Wi-Fi operations for SmartProv:
 *   - Non-blocking STA connection with configurable timeout
 *   - Fast-fail on auth rejection (wrong password / SSID not found)
 *   - Access Point startup for provisioning mode
 *   - Network scanning with deduplication and RSSI-descending sort
 *   - JSON serialisation of scan results for the captive portal UI
 *
 * Wrong-password detection — platform differences:
 *   ESP32:   WL_CONNECT_FAILED fires quickly on auth rejection.
 *            WL_DISCONNECTED (6) arrives at the timeout boundary.
 *            WL_WRONG_PASSWORD does not exist in the ESP32 core.
 *   ESP8266: WL_WRONG_PASSWORD (6) and WL_CONNECT_FAILED rarely fire
 *            before the timeout. At the timeout boundary, the SDK reports
 *            WL_DISCONNECTED (7) after auth rejection — the station is
 *            disconnected by the AP following a failed 4-way handshake.
 *            This is confirmed by hardware testing (finalStatus=7 on bad
 *            credentials, finalStatus=1 when SSID is absent).
 *   Both:    Define SP_DEBUG before including SmartProv.h to log each
 *            wl_status change during a connection attempt.
 */

#ifndef SP_WIFI_H
#define SP_WIFI_H

#include <Arduino.h>

#ifdef ESP32
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
    #define WIFI_AUTH_OPEN ENC_TYPE_NONE
#endif

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

#define SP_CONNECT_TIMEOUT_MS   8000
#define SP_MAX_RECONNECT_TRIES  5
#define SP_AP_CHANNEL           6
#define SP_AP_MAX_CLIENTS       4

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

enum SPWiFiState {
    SP_WIFI_IDLE,
    SP_WIFI_CONNECTING,
    SP_WIFI_CONNECTED,
    SP_WIFI_FAILED,
    SP_WIFI_AP_MODE
};

enum SPFailReason {
    SP_FAIL_NONE,
    SP_FAIL_WRONG_PASSWORD,
    SP_FAIL_SSID_NOT_FOUND,
    SP_FAIL_TIMEOUT
};

struct SPNetwork {
    String  ssid;
    int32_t rssi;
    bool    isOpen;
};

// ---------------------------------------------------------------------------
// SP_WiFi
// ---------------------------------------------------------------------------

class SP_WiFi {
public:

    SP_WiFi()
        : _state(SP_WIFI_IDLE),
          _connectStartTime(0),
          _reconnectAttempts(0),
          _lastFailReason(SP_FAIL_NONE)
    {}

    String generateAPName(const char* prefix = "SmartProv") {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char apName[32];
        snprintf(apName, sizeof(apName), "%s_%02X%02X", prefix, mac[4], mac[5]);
        return String(apName);
    }

    void beginSTA(const char* ssid, const char* password) {
        Serial.printf("[SmartProv] [WiFi] Connecting to: %s\n", ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        _state            = SP_WIFI_CONNECTING;
        _connectStartTime = millis();
        _lastFailReason   = SP_FAIL_NONE;
    }

    /**
     * Polls the current connection attempt.
     * Must be called every loop() iteration while state is SP_WIFI_CONNECTING.
     * Returns SP_WIFI_CONNECTED, SP_WIFI_FAILED, or SP_WIFI_CONNECTING.
     */
    SPWiFiState update() {
        if (_state != SP_WIFI_CONNECTING) return _state;

        const wl_status_t status = WiFi.status();

#ifdef SP_DEBUG
        static wl_status_t _dbgLastStatus = static_cast<wl_status_t>(-1);
        if (status != _dbgLastStatus) {
            Serial.printf("[SmartProv] [WiFi] [debug] wl_status=%d elapsed=%lums\n",
                          static_cast<int>(status), millis() - _connectStartTime);
            _dbgLastStatus = status;
        }
#endif

        if (status == WL_CONNECTED) {
            _state             = SP_WIFI_CONNECTED;
            _reconnectAttempts = 0;
            _lastFailReason    = SP_FAIL_NONE;
            Serial.printf("[SmartProv] [WiFi] Connected — IP: %s | RSSI: %d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return _state;
        }

        // WL_NO_SSID_AVAIL fires immediately when the SSID is absent on both platforms.
        if (status == WL_NO_SSID_AVAIL) {
            _lastFailReason = SP_FAIL_SSID_NOT_FOUND;
            _failNow();
            return _state;
        }

        // WL_CONNECT_FAILED fires quickly on ESP32 for auth rejection.
        // On ESP8266 it may appear mid-loop in some SDK versions.
        if (status == WL_CONNECT_FAILED) {
            _lastFailReason = SP_FAIL_WRONG_PASSWORD;
            _failNow();
            return _state;
        }

        if (millis() - _connectStartTime > SP_CONNECT_TIMEOUT_MS) {
            const wl_status_t finalStatus = WiFi.status();

#ifdef ESP32
            // ESP32: WL_DISCONNECTED (6) arrives after auth rejection at timeout.
            if (finalStatus == WL_DISCONNECTED || finalStatus == WL_CONNECT_FAILED) {
                _lastFailReason = SP_FAIL_WRONG_PASSWORD;
            } else if (finalStatus == WL_NO_SSID_AVAIL) {
                _lastFailReason = SP_FAIL_SSID_NOT_FOUND;
            } else {
                _lastFailReason = SP_FAIL_TIMEOUT;
            }
#else
            // ESP8266: WL_DISCONNECTED (7) arrives after auth rejection at timeout.
            // Hardware-confirmed: wrong password → finalStatus=7, missing SSID → finalStatus=1.
            if (finalStatus == WL_DISCONNECTED ||
                finalStatus == WL_WRONG_PASSWORD ||
                finalStatus == WL_CONNECT_FAILED) {
                _lastFailReason = SP_FAIL_WRONG_PASSWORD;
            } else if (finalStatus == WL_NO_SSID_AVAIL) {
                _lastFailReason = SP_FAIL_SSID_NOT_FOUND;
            } else {
                _lastFailReason = SP_FAIL_TIMEOUT;
            }
#endif

            _failNow();
        }

        return _state;
    }

    void startAP(const String& apName, const char* password = "") {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);

        bool ok = WiFi.softAP(
            apName.c_str(),
            (strlen(password) > 0) ? password : nullptr,
            SP_AP_CHANNEL,
            false,
            SP_AP_MAX_CLIENTS
        );

        if (ok) {
            _state = SP_WIFI_AP_MODE;
            Serial.printf("[SmartProv] [WiFi] AP started: %s\n",  apName.c_str());
            Serial.printf("[SmartProv] [WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
        } else {
            Serial.println("[SmartProv] [WiFi] ERROR: softAP() failed.");
        }
    }

    /**
     * Scans for available networks and returns a JSON array.
     * Each element: { "ssid": "...", "rssi": -42, "signal": 4, "open": false }
     * Results are deduplicated by SSID and sorted by RSSI descending.
     */
    String scanNetworks() {
        Serial.println("[SmartProv] [WiFi] Scanning networks...");
        int count = WiFi.scanNetworks();
        Serial.printf("[SmartProv] [WiFi] Scan complete — raw count: %d\n", count);

        if (count <= 0) return "[]";

        SPNetwork* nets = new SPNetwork[count];
        int netCount = 0;

        for (int i = 0; i < count; i++) {
            String ssid = WiFi.SSID(i);
            ssid.trim();
            if (ssid.length() == 0) continue;

            int32_t rssi = WiFi.RSSI(i);
            bool found = false;

            for (int j = 0; j < netCount; j++) {
                if (nets[j].ssid == ssid) {
                    if (rssi > nets[j].rssi) nets[j].rssi = rssi;
                    found = true;
                    break;
                }
            }

            if (!found) {
                nets[netCount].ssid   = ssid;
                nets[netCount].rssi   = rssi;
                nets[netCount].isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                netCount++;
            }
        }

        Serial.printf("[SmartProv] [WiFi] Unique networks: %d\n", netCount);

        for (int i = 0; i < netCount - 1; i++) {
            for (int j = 0; j < netCount - i - 1; j++) {
                if (nets[j].rssi < nets[j + 1].rssi) {
                    SPNetwork tmp  = nets[j];
                    nets[j]        = nets[j + 1];
                    nets[j + 1]    = tmp;
                }
            }
        }

        String json = "[";
        for (int i = 0; i < netCount; i++) {
            String ssid = nets[i].ssid;
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");

            json += "{\"ssid\":\"" + ssid + "\","
                  + "\"rssi\":"    + String(nets[i].rssi) + ","
                  + "\"signal\":"  + String(rssiToBars(nets[i].rssi)) + ","
                  + "\"open\":"    + (nets[i].isOpen ? "true" : "false")
                  + "}";

            if (i < netCount - 1) json += ",";
        }
        json += "]";

        delete[] nets;
        WiFi.scanDelete();

        return json;
    }

    // ---------------------------------------------------------------------------
    // Status accessors
    // ---------------------------------------------------------------------------

    SPWiFiState  getState()             const { return _state; }
    bool         isConnected()          const { return WiFi.status() == WL_CONNECTED; }
    int32_t      getRSSI()              const { return WiFi.RSSI(); }
    String       getIP()                const { return WiFi.localIP().toString(); }
    String       getMACAddress()        const { return WiFi.macAddress(); }
    int          getReconnectAttempts() const { return _reconnectAttempts; }
    SPFailReason getLastFailureReason() const { return _lastFailReason; }

    bool hasExceededReconnectLimit() const {
        return _reconnectAttempts >= SP_MAX_RECONNECT_TRIES;
    }

    bool isWrongPassword() const {
        return _lastFailReason == SP_FAIL_WRONG_PASSWORD;
    }

    void disconnect() {
        WiFi.disconnect(true);
        _state = SP_WIFI_IDLE;
    }

    void resetReconnectCounter() {
        _reconnectAttempts = 0;
    }

private:

    static int rssiToBars(int32_t rssi) {
        if (rssi >= -55) return 4;
        if (rssi >= -65) return 3;
        if (rssi >= -75) return 2;
        return 1;
    }

    void _failNow() {
        _state = SP_WIFI_FAILED;
        _reconnectAttempts++;
        _logFailureReason();
        WiFi.disconnect(true);
    }

    void _logFailureReason() const {
        switch (_lastFailReason) {
            case SP_FAIL_WRONG_PASSWORD:
                Serial.println("[SmartProv] [WiFi] FAILED — authentication rejected (wrong password).");
                break;
            case SP_FAIL_SSID_NOT_FOUND:
                Serial.println("[SmartProv] [WiFi] FAILED — SSID not found.");
                break;
            case SP_FAIL_TIMEOUT:
            default:
                Serial.printf("[SmartProv] [WiFi] FAILED — connection timed out (wl_status=%d).\n",
                              static_cast<int>(WiFi.status()));
                break;
        }
        Serial.printf("[SmartProv] [WiFi] Attempt %d / %d.\n",
                      _reconnectAttempts, SP_MAX_RECONNECT_TRIES);
    }

    SPWiFiState   _state;
    unsigned long _connectStartTime;
    int           _reconnectAttempts;
    SPFailReason  _lastFailReason;
};

#endif // SP_WIFI_H
