#include "Config.h"
#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "cfg";
}

void Config::begin() {
    Preferences prefs;
    prefs.begin(kNamespace, true);

    current_.wifiSsid = prefs.getString("wifi_ssid", "");
    current_.wifiPassword = prefs.getString("wifi_pass", "");
    current_.woffuUsername = prefs.getString("woffu_user", "");
    current_.woffuPassword = prefs.getString("woffu_pass", "");
    current_.activeWindow.startMinutes = prefs.getUShort("win_s", 450);
    current_.activeWindow.endMinutes = prefs.getUShort("win_e", 1140);
    current_.forceActiveWindow = prefs.getBool("force_active", false);
    learnedCardUid_ = prefs.getString("nfc_uid", "");

    current_.autoSignEnabled = prefs.getBool("as_en", false);
    for (int i = 0; i < 5; i++) {
        char keyStart[8];
        char keyEnd[8];
        snprintf(keyStart, sizeof(keyStart), "as_s%d", i);
        snprintf(keyEnd, sizeof(keyEnd), "as_e%d", i);
        current_.autoSignSchedule[i].startMinutes =
            prefs.getUShort(keyStart, current_.autoSignSchedule[i].startMinutes);
        current_.autoSignSchedule[i].endMinutes =
            prefs.getUShort(keyEnd, current_.autoSignSchedule[i].endMinutes);
    }

    prefs.end();
}

const DeviceConfig& Config::get() const {
    return current_;
}

bool Config::save(const DeviceConfig& config) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }

    prefs.putString("wifi_ssid", config.wifiSsid);
    prefs.putString("wifi_pass", config.wifiPassword);
    prefs.putString("woffu_user", config.woffuUsername);
    prefs.putString("woffu_pass", config.woffuPassword);
    prefs.putUShort("win_s", config.activeWindow.startMinutes);
    prefs.putUShort("win_e", config.activeWindow.endMinutes);
    prefs.putBool("force_active", config.forceActiveWindow);

    prefs.putBool("as_en", config.autoSignEnabled);
    for (int i = 0; i < 5; i++) {
        char keyStart[8];
        char keyEnd[8];
        snprintf(keyStart, sizeof(keyStart), "as_s%d", i);
        snprintf(keyEnd, sizeof(keyEnd), "as_e%d", i);
        prefs.putUShort(keyStart, config.autoSignSchedule[i].startMinutes);
        prefs.putUShort(keyEnd, config.autoSignSchedule[i].endMinutes);
    }

    prefs.end();

    current_ = config;
    return true;
}

void Config::markOtaPending(const String& fromVersion, const String& toVersion) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }
    prefs.putString("ota_from", fromVersion);
    prefs.putString("ota_to", toVersion);
    prefs.end();
}

void Config::clearOtaNote() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }
    prefs.remove("ota_from");
    prefs.remove("ota_to");
    prefs.end();
}

bool Config::takeOtaNote(String& fromVersion, String& toVersion) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    toVersion = prefs.getString("ota_to", "");
    if (toVersion.isEmpty()) {
        prefs.end();
        return false;
    }
    fromVersion = prefs.getString("ota_from", "");
    prefs.remove("ota_from");
    prefs.remove("ota_to");
    prefs.end();
    return true;
}

bool Config::hasLearnedCard() const {
    return !learnedCardUid_.isEmpty();
}

String Config::learnedCardUid() const {
    return learnedCardUid_;
}

void Config::setLearnedCardUid(const String& uidHex) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }
    prefs.putString("nfc_uid", uidHex);
    prefs.end();
    learnedCardUid_ = uidHex;
}

void Config::factoryReset() {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
    current_ = DeviceConfig{};
    learnedCardUid_ = "";
}
