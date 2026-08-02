#pragma once

#include <Arduino.h>
#include <functional>

enum class OtaCheckResult : uint8_t { UP_TO_DATE, AVAILABLE, ERROR };
enum class OtaResult : uint8_t { UP_TO_DATE, UPDATED, ERROR };

class OtaUpdater {
public:
    // Solo comprueba si hay una version mas reciente (GET version.txt), sin
    // descargar ni flashear nada.
    OtaCheckResult checkForUpdate(String& latestVersionOut);

    // Descarga e instala targetVersion (normalmente la que devolvio la ultima
    // llamada a checkForUpdate() con exito, ver AppStateMachine); no vuelve a
    // comprobar version.txt, para no repetir un round-trip HTTPS entero justo
    // antes de empezar a descargar. Si tiene exito el dispositivo se reinicia
    // solo (ver rebootOnUpdate en el .cpp) y esta llamada nunca retorna.
    OtaResult update(const String& targetVersion);

    const String& lastErrorDetail() const { return lastErrorDetail_; }

    // Progreso de la descarga/flasheo (bytes actuales, bytes totales), llamado
    // desde dentro de update(). Permite a quien lo registre (ver
    // AppStateMachine) seguir atendiendo el portal web mientras update() esta
    // bloqueada descargando.
    void setProgressCallback(std::function<void(size_t current, size_t total)> callback) {
        progress_ = callback;
    }

    // Se invoca justo antes de httpUpdate.update(), el punto sin retorno
    // antes del reboot automatico en caso de exito (ver .cpp): es la unica
    // oportunidad de dejar constancia de que se encontro una version nueva
    // en algo que sobreviva al reboot (el propio update() nunca llega a
    // devolver el control si el flasheo tiene exito).
    void setBeforeFlashCallback(std::function<void(const String& from, const String& to)> callback) {
        beforeFlash_ = callback;
    }

private:
    bool fetchLatestVersion(String& out);

    String lastErrorDetail_;
    std::function<void(const String&, const String&)> beforeFlash_;
    std::function<void(size_t, size_t)> progress_;
};
