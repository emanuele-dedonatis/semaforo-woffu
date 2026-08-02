#pragma once

#include <Arduino.h>
#include "config/Config.h"
#include "net/WoffuClient.h"

enum class PollMode : uint8_t { OFF, PASSIVE, ACTIVE };

class Scheduler {
public:
    void configure(const DeviceConfig& config);

    // Decide el modo de polling segun la ventana de encendido/apagado configurada
    // por el usuario y la jornada del dia que reporta Woffu (ventana pasiva de
    // fichaje, fin de semana, festivo). Deja en `reason` una explicacion en
    // castellano de la decision, para logging.
    PollMode currentMode(uint16_t nowMinutesOfDay, const WorkdayInfo& workday, bool workdayValid,
                          String& reason) const;
    uint16_t pollIntervalSeconds(PollMode mode) const;

private:
    DeviceConfig config_;
};
