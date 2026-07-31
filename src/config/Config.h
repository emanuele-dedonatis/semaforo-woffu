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
};

class Config {
public:
    void begin();
    const DeviceConfig& get() const;
    bool save(const DeviceConfig& config);
    void factoryReset();

private:
    DeviceConfig current_;
};
