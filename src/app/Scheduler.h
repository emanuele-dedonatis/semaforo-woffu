#pragma once

#include <Arduino.h>
#include "config/Config.h"

enum class PollMode : uint8_t { OFF, PASSIVE, ACTIVE };

class Scheduler {
public:
    void configure(const DeviceConfig& config);
    PollMode currentMode(uint16_t nowMinutesOfDay, uint8_t isoWeekday) const;
    uint16_t pollIntervalSeconds(PollMode mode) const;

private:
    DeviceConfig config_;
};
