#include "Scheduler.h"

namespace {
// Ritmos de polling fijos (ver Requisitos.md); ya no son configurables desde el portal.
constexpr uint16_t kPollActiveSeconds = 60;
constexpr uint16_t kPollPassiveSeconds = 900;
}  // namespace

void Scheduler::configure(const DeviceConfig& config) {
    config_ = config;
}

PollMode Scheduler::currentMode(uint16_t nowMinutesOfDay, const WorkdayInfo& workday, bool workdayValid,
                                 WoffuStatus lastStatus, String& reason) const {
    const TimeWindow& window = config_.activeWindow;
    if (nowMinutesOfDay < window.startMinutes || nowMinutesOfDay >= window.endMinutes) {
        reason = "fuera de la ventana de encendido configurada";
        return PollMode::OFF;
    }

    if (!workdayValid) {
        reason = "sin datos de jornada de Woffu, se asume ventana activa";
        return PollMode::ACTIVE;
    }

    if (workday.isWeekend) {
        reason = "hoy es fin de semana segun Woffu";
        return PollMode::OFF;
    }
    if (workday.isHoliday) {
        reason = "hoy es festivo segun Woffu";
        return PollMode::OFF;
    }

    if (nowMinutesOfDay >= workday.startMinutes && nowMinutesOfDay < workday.endMinutes) {
        reason = "dentro de la ventana pasiva de fichaje de Woffu";
        return PollMode::PASSIVE;
    }

    if (nowMinutesOfDay < workday.startMinutes && lastStatus == WoffuStatus::CLOCKED_IN) {
        reason = "ya fichada la entrada antes de la ventana pasiva, se relaja el ritmo de sondeo";
        return PollMode::PASSIVE;
    }
    if (nowMinutesOfDay >= workday.endMinutes && lastStatus == WoffuStatus::CLOCKED_OUT) {
        reason = "ya fichada la salida despues de la ventana pasiva, se relaja el ritmo de sondeo";
        return PollMode::PASSIVE;
    }

    reason = "fuera de la ventana pasiva de fichaje de Woffu";
    return PollMode::ACTIVE;
}

uint16_t Scheduler::pollIntervalSeconds(PollMode mode) const {
    switch (mode) {
        case PollMode::ACTIVE:
            return kPollActiveSeconds;
        case PollMode::PASSIVE:
            return kPollPassiveSeconds;
        default:
            return 0;
    }
}
