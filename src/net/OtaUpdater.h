#pragma once

#include <Arduino.h>
#include <functional>

enum class OtaResult : uint8_t { UP_TO_DATE, UPDATED, ERROR };

class OtaUpdater {
public:
    OtaResult checkAndUpdate();
    const String& lastErrorDetail() const { return lastErrorDetail_; }

    // Se invoca justo antes de httpUpdate.update(), el punto sin retorno
    // antes del reboot automatico en caso de exito (ver .cpp): es la unica
    // oportunidad de dejar constancia de que se encontro una version nueva
    // en algo que sobreviva al reboot (el propio checkAndUpdate() nunca
    // llega a devolver el control si el flasheo tiene exito).
    void setBeforeFlashCallback(std::function<void(const String& from, const String& to)> callback) {
        beforeFlash_ = callback;
    }

private:
    String lastErrorDetail_;
    std::function<void(const String&, const String&)> beforeFlash_;
};
