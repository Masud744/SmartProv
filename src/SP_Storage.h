/**
 * SP_Storage.h — SmartProv Persistent Storage Module
 * ====================================================
 * Provides a unified storage interface for both ESP32 (Preferences/NVS)
 * and ESP8266 (EEPROM). Handles serialization and deserialization of
 * the full device configuration, including multi-network credentials,
 * device identity, and user-defined custom fields.
 *
 * ESP32  : Uses the Preferences library (NVS partition, key-value store).
 * ESP8266: Uses the EEPROM emulation library (flash-backed byte array).
 *
 * Storage layout version is enforced via EEPROM_VALID_FLAG on ESP8266.
 * Changing SP_MAX_NETWORKS, SP_MAX_FIELDS, or struct layout requires
 * incrementing this flag to invalidate stale stored configs.
 */

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

// ---------------------------------------------------------------------------
// Capacity constants — adjustable via user defines before including this header
// ---------------------------------------------------------------------------

#ifndef SP_MAX_NETWORKS
    #define SP_MAX_NETWORKS  3
#endif

#ifndef SP_MAX_FIELDS
    #define SP_MAX_FIELDS    4
#endif

#define SP_FIELD_KEY_LEN  16
#define SP_FIELD_VAL_LEN  64

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// SP_Storage
// ---------------------------------------------------------------------------

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
            strncpy(config.networks[i].ssid,     _prefs.getString(ssidKey.c_str(), "").c_str(), 63);
            strncpy(config.networks[i].password, _prefs.getString(pwKey.c_str(),   "").c_str(), 63);
            config.networks[i].valid = true;
        }

        strncpy(config.deviceName, _prefs.getString("deviceName", "SmartDevice").c_str(), 31);

        for (int i = 0; i < SP_MAX_FIELDS; i++) {
            const String flagKey = "ff" + String(i);
            if (!_prefs.getBool(flagKey.c_str(), false)) continue;

            const String kKey = "fk" + String(i);
            const String vKey = "fv" + String(i);
            strncpy(config.fields[i].key,   _prefs.getString(kKey.c_str(), "").c_str(), SP_FIELD_KEY_LEN - 1);
            strncpy(config.fields[i].value, _prefs.getString(vKey.c_str(), "").c_str(), SP_FIELD_VAL_LEN - 1);
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

        Serial.println("[Storage] Configuration saved.");
    }

    void clear() {
#ifdef ESP32
        _prefs.clear();
#else
        EEPROM.write(0, 0x00);
        EEPROM.commit();
#endif
        Serial.println("[Storage] Configuration cleared.");
    }

    // Returns the value of a named custom field, or an empty String if not found.
    String getField(const SPConfig& config, const char* key) const {
        for (int i = 0; i < SP_MAX_FIELDS; i++) {
            if (config.fields[i].valid && strcmp(config.fields[i].key, key) == 0) {
                return String(config.fields[i].value);
            }
        }
        return String();
    }

    // Returns the first valid WiFi entry, or a zeroed entry if none exist.
    SPWiFiEntry getFirstNetwork(const SPConfig& config) const {
        for (int i = 0; i < SP_MAX_NETWORKS; i++) {
            if (config.networks[i].valid) return config.networks[i];
        }
        SPWiFiEntry empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }

private:
#ifdef ESP32
    Preferences _prefs;
#endif
};

#endif // SP_STORAGE_H
