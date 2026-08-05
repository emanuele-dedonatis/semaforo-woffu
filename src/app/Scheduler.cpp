#include "Scheduler.h"

void Scheduler::configure(const DeviceConfig& config) {
    config_ = config;
}

PollMode Scheduler::currentMode(uint16_t nowMinutesOfDay, const WorkdayInfo& workday, bool workdayValid,
                                 String& reason) const {
    const TimeWindow& window = config_.activeWindow;
    if (nowMinutesOfDay < window.startMinutes || nowMinutesOfDay >= window.endMinutes) {
        reason = "fuera de la ventana de encendido configurada";
        return PollMode::OFF;
    }

    if (!workdayValid) {
        reason = "sin datos de jornada de Woffu, se asume dispositivo activo";
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

    reason = "dentro de la ventana de encendido configurada";
    return PollMode::ACTIVE;
}
