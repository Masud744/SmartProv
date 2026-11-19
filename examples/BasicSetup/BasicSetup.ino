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
    }
}
