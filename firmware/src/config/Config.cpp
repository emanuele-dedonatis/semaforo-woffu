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
    learnedCardUid_ = prefs.getString("nfc_uid", "");
    // Sin clave guardada = URL por defecto; guardada vacia = deshabilitado.
    current_.remoteUrl = prefs.isKey("rem_url") ? prefs.getString("rem_url", "") : String(kDefaultRemoteUrl);
    current_.devicePassword = prefs.getString("dev_pass", "");
    current_.otaAutoUpdate = prefs.getBool("ota_auto", true);
    // led_ovr: 0 = sin override; si no, 0x80 | (color << 4) | mode (ver setLedOverride()).
    uint8_t ovr = prefs.getUChar("led_ovr", 0);
    hasLedOverride_ = ovr != 0;
    ledOverrideColor_ = (ovr >> 4) & 0x07;
    ledOverrideMode_ = ovr & 0x0F;

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

    prefs.putString("rem_url", config.remoteUrl);
    prefs.putString("dev_pass", config.devicePassword);
    prefs.putBool("ota_auto", config.otaAutoUpdate);
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

bool validateConfig(const DeviceConfig& config, String& error) {
    if (config.wifiSsid.isEmpty()) {
        error = "falta la red WiFi";
        return false;
    }
    if (config.woffuUsername.isEmpty()) {
        error = "falta el usuario de Woffu";
        return false;
    }
    if (!config.remoteUrl.isEmpty() && !config.remoteUrl.startsWith("wss://") &&
        !config.remoteUrl.startsWith("ws://")) {
        error = "la URL del servidor debe empezar por wss:// o ws://";
        return false;
    }
    size_t passLen = config.devicePassword.length();
    if (passLen != 0 && (passLen < 8 || passLen > 63)) {
        error = "la password del dispositivo debe tener entre 8 y 63 caracteres (o dejarse vacia)";
        return false;
    }
    for (int i = 0; i < 5; i++) {
        if (config.autoSignSchedule[i].startMinutes >= 1440 || config.autoSignSchedule[i].endMinutes >= 1440) {
            error = "horario de fichaje automatico no valido";
            return false;
        }
    }
    if (config.activeWindow.startMinutes >= 1440 || config.activeWindow.endMinutes >= 1440) {
        error = "horario de encendido no valido";
        return false;
    }
    return true;
}

bool Config::ledOverride(uint8_t& color, uint8_t& mode) const {
    if (!hasLedOverride_) {
        return false;
    }
    color = ledOverrideColor_;
    mode = ledOverrideMode_;
    return true;
}

void Config::setLedOverride(uint8_t color, uint8_t mode) {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        // El bit fijo 0x80 garantiza que el valor guardado nunca sea 0 (= sin
        // override) aunque color y mode sean ambos 0 (LedColor::OFF/LedMode::OFF).
        prefs.putUChar("led_ovr", static_cast<uint8_t>(0x80 | ((color & 0x07) << 4) | (mode & 0x0F)));
        prefs.end();
    }
    hasLedOverride_ = true;
    ledOverrideColor_ = color & 0x07;
    ledOverrideMode_ = mode & 0x0F;
}

void Config::clearLedOverride() {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.remove("led_ovr");
        prefs.end();
    }
    hasLedOverride_ = false;
}

void Config::factoryReset() {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
    current_ = DeviceConfig{};
    learnedCardUid_ = "";
    hasLedOverride_ = false;
}
