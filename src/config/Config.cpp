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
    current_.timezone = prefs.getString("tz", "UTC0");
    current_.windowIn.startMinutes = prefs.getUShort("win_in_s", 450);
    current_.windowIn.endMinutes = prefs.getUShort("win_in_e", 540);
    current_.windowOut.startMinutes = prefs.getUShort("win_out_s", 840);
    current_.windowOut.endMinutes = prefs.getUShort("win_out_e", 1080);
    current_.pollActiveSeconds = prefs.getUShort("poll_act_s", 45);
    current_.pollPassiveSeconds = prefs.getUShort("poll_pas_s", 900);
    current_.brightness = prefs.getUChar("brightness", 180);

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
    prefs.putString("tz", config.timezone);
    prefs.putUShort("win_in_s", config.windowIn.startMinutes);
    prefs.putUShort("win_in_e", config.windowIn.endMinutes);
    prefs.putUShort("win_out_s", config.windowOut.startMinutes);
    prefs.putUShort("win_out_e", config.windowOut.endMinutes);
    prefs.putUShort("poll_act_s", config.pollActiveSeconds);
    prefs.putUShort("poll_pas_s", config.pollPassiveSeconds);
    prefs.putUChar("brightness", config.brightness);

    prefs.end();

    current_ = config;
    current_.configured = true;
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
