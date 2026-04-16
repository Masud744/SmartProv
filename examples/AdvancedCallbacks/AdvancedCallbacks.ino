/**
 * SmartProv — Example: AdvancedCallbacks
 * ========================================
 * Demonstrates custom pin assignment, the onConnected() and onLoop()
 * callbacks, and a serial command interface for triggering a software reset.
 *
 * Configuration overrides must be defined before including SmartProv.h.
 */

#define SP_AP_PREFIX      "IoTDevice"
#define SP_RESET_PIN      0
#define SP_LED_PIN        2
#define SP_RESET_HOLD_MS  5000

#include <SmartProv.h>

SmartProv prov;

void onWiFiConnected() {
    Serial.println("[App] WiFi connected.");
    Serial.printf("[App] Device  : %s\n", prov.getDeviceName().c_str());
    Serial.printf("[App] Network : %s\n", prov.getSSID().c_str());
    Serial.printf("[App] IP      : %s\n", prov.getIP().c_str());
    Serial.printf("[App] RSSI    : %d dBm\n", prov.getRSSI());
    Serial.printf("[App] MAC     : %s\n", prov.getMACAddress().c_str());

    // Initialize WiFi-dependent services here (MQTT, NTP, HTTP client, etc.)
}

void onWiFiLoop() {
    static unsigned long lastSample = 0;
    if (millis() - lastSample > 2000) {
        lastSample = millis();
        // Sensor read / data transmission logic goes here.
    }
}

void setup() {
    Serial.begin(115200);
    prov.begin(SP_RESET_PIN, SP_LED_PIN);
    prov.onConnected(onWiFiConnected);
    prov.onLoop(onWiFiLoop);
}

void loop() {
    prov.update();

    if (Serial.available()) {
        const char cmd = Serial.read();
        if (cmd == 'R' || cmd == 'r') {
            Serial.println("[Command] Clearing credentials and restarting...");
            prov.resetCredentials();
        }
    }
}
