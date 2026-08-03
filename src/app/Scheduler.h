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
    // fichaje, fin de semana, festivo). `lastStatus` es el ultimo estado conocido
    // de Woffu (de la ultima consulta real, puede ser UNKNOWN si aun no se ha
    // hecho ninguna): si ya se ficho la entrada antes de la ventana pasiva, o la
    // salida despues de ella, se relaja a ventana pasiva sin esperar a que
    // cambie la hora, porque ya no hace falta reaccionar rapido a un cambio que
    // ya ocurrio. Deja en `reason` una explicacion en castellano de la decision,
    // para logging.
    PollMode currentMode(uint16_t nowMinutesOfDay, const WorkdayInfo& workday, bool workdayValid,
                          WoffuStatus lastStatus, String& reason) const;
    uint16_t pollIntervalSeconds(PollMode mode) const;

private:
    DeviceConfig config_;
};
