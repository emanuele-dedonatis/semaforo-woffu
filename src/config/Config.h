#pragma once

#include <Arduino.h>

struct TimeWindow {
    uint16_t startMinutes = 0;
    uint16_t endMinutes = 0;
};

struct DeviceConfig {
    bool configured = false;
    String wifiSsid;
    String wifiPassword;
    String woffuUsername;
    String woffuPassword;
    String timezone;
    TimeWindow windowIn;
    TimeWindow windowOut;
    uint16_t pollActiveSeconds = 45;
    uint16_t pollPassiveSeconds = 900;
    uint8_t brightness = 180;
    bool forceActiveWindow = false;
};

class Config {
public:
    void begin();
    const DeviceConfig& get() const;
    bool save(const DeviceConfig& config);
    void factoryReset();

    // Nota de "hay un OTA en curso" que sobrevive al reboot que dispara un
    // flasheo con exito, para poder mostrar el resultado en el portal la
    // proxima vez que arranca (ver AppStateMachine y OtaUpdater).
    void markOtaPending(const String& fromVersion, const String& toVersion);
    void clearOtaNote();
    bool takeOtaNote(String& fromVersion, String& toVersion);

private:
    DeviceConfig current_;
};
