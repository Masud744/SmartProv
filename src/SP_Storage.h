#ifndef SP_STORAGE_H
#define SP_STORAGE_H

#include <Arduino.h>

#ifdef ESP32
    #include <Preferences.h>
#else
    #include <EEPROM.h>
    #define EEPROM_SIZE       1280
    #define EEPROM_VALID_FLAG 0xAC
#endif

#ifndef SP_MAX_NETWORKS
    #define SP_MAX_NETWORKS  3
#endif

#ifndef SP_MAX_FIELDS
    #define SP_MAX_FIELDS    4
#endif

#define SP_FIELD_KEY_LEN  16
#define SP_FIELD_VAL_LEN  64

struct SPWiFiEntry {
    char ssid[64];
    char password[64];
    bool valid;
};

struct SPField {
    char key[SP_FIELD_KEY_LEN];
    char value[SP_FIELD_VAL_LEN];
    bool valid;
};

struct SPConfig {
    SPWiFiEntry networks[SP_MAX_NETWORKS];
    char        deviceName[32];
    SPField     fields[SP_MAX_FIELDS];
    bool        isConfigured;
};

class SP_Storage {
public:

    void init() {
#ifdef ESP32
        _prefs.begin("smartprov", false);
#else
        EEPROM.begin(EEPROM_SIZE);
#endif
    }

    SPConfig load() {
        SPConfig config;
        memset(&config, 0, sizeof(config));

#ifdef ESP32
        for (int i = 0; i < SP_MAX_NETWORKS; i++) {
            const String validKey = "valid" + String(i);
            if (!_prefs.getBool(validKey.c_str(), false)) continue;

            const String ssidKey = "ssid" + String(i);
            const String pwKey   = "pw"   + String(i);
            String tmpSsid = _prefs.getString(ssidKey.c_str(), "");
            strncpy(config.networks[i].ssid, tmpSsid.c_str(), sizeof(config.networks[i].ssid) - 1);
            config.networks[i].ssid[sizeof(config.networks[i].ssid) - 1] = '\0';

            String tmpPw = _prefs.getString(pwKey.c_str(), "");
            strncpy(config.networks[i].password, tmpPw.c_str(), sizeof(config.networks[i].password) - 1);
            config.networks[i].password[sizeof(config.networks[i].password) - 1] = '\0';
            config.networks[i].valid = true;
        }

        String tmpDevName = _prefs.getString("deviceName", "SmartDevice");
        strncpy(config.deviceName, tmpDevName.c_str(), sizeof(config.deviceName) - 1);
        config.deviceName[sizeof(config.deviceName) - 1] = '\0';

        for (int i = 0; i < SP_MAX_FIELDS; i++) {
            const String flagKey = "ff" + String(i);
            if (!_prefs.getBool(flagKey.c_str(), false)) continue;

            const String kKey = "fk" + String(i);
            const String vKey = "fv" + String(i);
            String tmpKey = _prefs.getString(kKey.c_str(), "");
            strncpy(config.fields[i].key, tmpKey.c_str(), SP_FIELD_KEY_LEN - 1);
            config.fields[i].key[SP_FIELD_KEY_LEN - 1] = '\0';

            String tmpVal = _prefs.getString(vKey.c_str(), "");
            strncpy(config.fields[i].value, tmpVal.c_str(), SP_FIELD_VAL_LEN - 1);
            config.fields[i].value[SP_FIELD_VAL_LEN - 1] = '\0';
            config.fields[i].valid = true;
        }

        config.isConfigured = _prefs.getBool("configured", false);

#else
        if (EEPROM.read(0) == EEPROM_VALID_FLAG) {
            EEPROM.get(1, config);
            config.isConfigured = true;
        } else {
            config.isConfigured = false;
        }
#endif

        return config;
    }

    void save(const SPConfig& config) {
#ifdef ESP32
        for (int i = 0; i < SP_MAX_NETWORKS; i++) {
            if (!config.networks[i].valid) continue;

            _prefs.putString(("ssid"  + String(i)).c_str(), config.networks[i].ssid);
            _prefs.putString(("pw"    + String(i)).c_str(), config.networks[i].password);
            _prefs.putBool(  ("valid" + String(i)).c_str(), true);
        }

        _prefs.putString("deviceName", config.deviceName);

        for (int i = 0; i < SP_MAX_FIELDS; i++) {
            if (!config.fields[i].valid) continue;

            _prefs.putString(("fk" + String(i)).c_str(), config.fields[i].key);
            _prefs.putString(("fv" + String(i)).c_str(), config.fields[i].value);
            _prefs.putBool(  ("ff" + String(i)).c_str(), true);
        }

        _prefs.putBool("configured", true);

#else
        EEPROM.write(0, EEPROM_VALID_FLAG);
        EEPROM.put(1, config);
        EEPROM.commit();
#endif

        Serial.println(F("[Storage] Configuration saved."));
    }

    void clear() {
#ifdef ESP32
        _prefs.clear();
#else
        EEPROM.write(0, 0x00);
        EEPROM.commit();
#endif
        Serial.println(F("[Storage] Configuration cleared."));
    }

    String getField(const SPConfig& config, const char* key) const {
        for (int i = 0; i < SP_MAX_FIELDS; i++) {
            if (config.fields[i].valid && strcmp(config.fields[i].key, key) == 0) {
                return String(config.fields[i].value);
            }
        }
        return String();
    }

private:
#ifdef ESP32
    Preferences _prefs;
#endif
};

#endif // SP_STORAGE_H
