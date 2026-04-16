/**
 * SmartProv — Example: BasicSetup
 * =================================
 * Minimal SmartProv integration. Demonstrates the core provisioning
 * flow with no custom configuration.
 *
 * Provisioning flow:
 *   1. Upload to ESP32 or ESP8266.
 *   2. Open Serial Monitor at 115200 baud.
 *   3. Connect a phone or PC to the "SmartProv_XXXX" access point.
 *   4. The captive portal opens automatically; enter WiFi credentials.
 *   5. The device restarts, connects to the configured network, and
 *      begins printing status to the Serial Monitor every 5 seconds.
 *
 * To reconfigure: hold the reset button (GPIO 0) for 3 seconds, or
 * send 'R' via the Serial Monitor in the AdvancedCallbacks example.
 */

#include <SmartProv.h>

SmartProv prov;

void setup() {
    Serial.begin(115200);
    prov.begin();
}

void loop() {
    prov.update();

    if (prov.isConnected()) {
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 5000) {
            lastPrint = millis();
            Serial.println("=============================");
            Serial.print("Device : "); Serial.println(prov.getDeviceName());
            Serial.print("Network: "); Serial.println(prov.getSSID());
            Serial.print("IP     : "); Serial.println(prov.getIP());
            Serial.println("=============================");
        }

        // Application code goes here.
    }
}
