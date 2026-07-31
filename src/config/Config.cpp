#include "Config.h"

void Config::begin() {
}

const DeviceConfig& Config::get() const {
    return current_;
}

bool Config::save(const DeviceConfig& config) {
    current_ = config;
    return true;
}

void Config::factoryReset() {
    current_ = DeviceConfig{};
}
