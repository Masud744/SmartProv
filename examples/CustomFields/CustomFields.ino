/**
 * SmartProv — Example: CustomFields (v3.0.0)
 * ============================================
 * Demonstrates dynamic custom field injection into the provisioning form
 * via addField(), retrieval of saved values via getField() after boot,
 * mDNS discovery, and captive portal OTA firmware upload support.
 *
 * Use case: an IoT sensor node that requires an API key and a remote
 * server URL in addition to WiFi credentials. Both are collected during
 * the provisioning flow and persisted to flash alongside network config.
 */

#define SP_AP_PREFIX "SensorNode"

#include <SmartProv.h>

SmartProv prov;

void setup() {
    Serial.begin(115200);

    // Register custom fields before calling begin().
    // Each field appears as a labeled text input in the setup form.
    // Values are stored to flash and retrievable after restart via getField().
    prov.addField("api_key",    "API Key",    "e.g. abc123xyz789");
    prov.addField("server_url", "Server URL", "http://example.com/api");

    // Enable mDNS discovery (accessible at http://sensornode.local) and OTA updates
    prov.enableMDNS("sensornode");
    prov.enableOTA();

    prov.begin();

    prov.onConnected([]() {
        Serial.println("[App] Connected. Reading stored configuration...");
        Serial.printf("[App] Network   : %s\n", prov.getSSID().c_str());
        Serial.printf("[App] IP        : %s\n", prov.getIP().c_str());
        Serial.printf("[App] RSSI      : %d dBm\n", prov.getRSSI());
        Serial.printf("[App] MAC       : %s\n", prov.getMACAddress().c_str());

        const String apiKey    = prov.getField("api_key");
        const String serverUrl = prov.getField("server_url");

        Serial.printf("[App] API Key   : %s\n", apiKey.length() ? apiKey.c_str() : "(not set)");
        Serial.printf("[App] Server URL: %s\n", serverUrl.length() ? serverUrl.c_str() : "(not set)");

        if (apiKey.length() == 0) {
            Serial.println("[App] Warning: API key is empty. Reset and reconfigure.");
        }
    });

    prov.onLoop([]() {
        static unsigned long lastReport = 0;
        if (millis() - lastReport > 10000) {
            lastReport = millis();
            Serial.printf("[App] Signal: %d dBm\n", prov.getRSSI());
            // Data transmission logic goes here.
        }
    });
}

void loop() {
    prov.update();
}
