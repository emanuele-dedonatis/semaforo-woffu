#include "Scheduler.h"

void Scheduler::configure(const DeviceConfig& config) {
    config_ = config;
}

PollMode Scheduler::currentMode(uint16_t nowMinutesOfDay, uint8_t isoWeekday) const {
    return PollMode::OFF;
}

uint16_t Scheduler::pollIntervalSeconds(PollMode mode) const {
    switch (mode) {
        case PollMode::ACTIVE:
            return config_.pollActiveSeconds;
        case PollMode::PASSIVE:
            return config_.pollPassiveSeconds;
        default:
            return 0;
    }
}
