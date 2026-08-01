#include "Scheduler.h"

void Scheduler::configure(const DeviceConfig& config) {
    config_ = config;
}

PollMode Scheduler::currentMode(uint16_t nowMinutesOfDay, uint8_t isoWeekday) const {
    if (config_.forceActiveWindow) {
        return PollMode::ACTIVE;
    }

    constexpr uint8_t kIsoSaturday = 6;
    if (isoWeekday >= kIsoSaturday) {
        return PollMode::OFF;
    }

    const TimeWindow& windowIn = config_.windowIn;
    const TimeWindow& windowOut = config_.windowOut;

    if (nowMinutesOfDay >= windowIn.startMinutes && nowMinutesOfDay < windowIn.endMinutes) {
        return PollMode::ACTIVE;
    }
    if (nowMinutesOfDay >= windowOut.startMinutes && nowMinutesOfDay < windowOut.endMinutes) {
        return PollMode::ACTIVE;
    }
    if (nowMinutesOfDay >= windowIn.endMinutes && nowMinutesOfDay < windowOut.startMinutes) {
        return PollMode::PASSIVE;
    }
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
