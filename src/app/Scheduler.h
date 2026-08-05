#pragma once

#include <Arduino.h>
#include "config/Config.h"
#include "net/WoffuClient.h"

enum class PollMode : uint8_t { OFF, ACTIVE };

class Scheduler {
public:
    void configure(const DeviceConfig& config);

    // Decide si el dispositivo debe estar encendido y sondeando Woffu, segun
    // la ventana de encendido/apagado configurada por el usuario y si Woffu
    // marca el dia de hoy como fin de semana o festivo. Deja en `reason` una
    // explicacion en castellano de la decision, para logging.
    PollMode currentMode(uint16_t nowMinutesOfDay, const WorkdayInfo& workday, bool workdayValid,
                          String& reason) const;

private:
    DeviceConfig config_;
};
