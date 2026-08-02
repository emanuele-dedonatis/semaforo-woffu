#include "Config.h"
#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "cfg";
}

void Config::begin() {
    Preferences prefs;
    prefs.begin(kNamespace, true);

    current_.configured = prefs.getBool("configured", false);
    current_.wifiSsid = prefs.getString("wifi_ssid", "");
    current_.wifiPassword = prefs.getString("wifi_pass", "");
    current_.woffuUsername = prefs.getString("woffu_user", "");
    current_.woffuPassword = prefs.getString("woffu_pass", "");
    current_.activeWindow.startMinutes = prefs.getUShort("win_s", 450);
    current_.activeWindow.endMinutes = prefs.getUShort("win_e", 1140);
    current_.forceActiveWindow = prefs.getBool("force_active", false);

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

    prefs.putBool("configured", true);
    prefs.putString("wifi_ssid", config.wifiSsid);
    prefs.putString("wifi_pass", config.wifiPassword);
    prefs.putString("woffu_user", config.woffuUsername);
    prefs.putString("woffu_pass", config.woffuPassword);
    prefs.putUShort("win_s", config.activeWindow.startMinutes);
    prefs.putUShort("win_e", config.activeWindow.endMinutes);
    prefs.putBool("force_active", config.forceActiveWindow);

    prefs.end();

    current_ = config;
    current_.configured = true;
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

void Config::factoryReset() {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
    current_ = DeviceConfig{};
}
