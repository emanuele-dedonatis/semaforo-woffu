#pragma once

#include <Arduino.h>

struct TimeWindow {
    uint16_t startMinutes = 0;
    uint16_t endMinutes = 0;
    TimeWindow() = default;
    TimeWindow(uint16_t start, uint16_t end) : startMinutes(start), endMinutes(end) {}
};

struct DeviceConfig {
    String wifiSsid;
    String wifiPassword;
    String woffuUsername;
    String woffuPassword;
    // Ventana de encendido/apagado configurada por el usuario; dentro de ella el
    // Scheduler decide activa/pasiva segun la jornada que reporta Woffu (ver Scheduler).
    // Por defecto 07:30-19:00.
    TimeWindow activeWindow{450, 1140};
    bool forceActiveWindow = false;
};

class Config {
public:
    void begin();
    const DeviceConfig& get() const;
    bool save(const DeviceConfig& config);
    void factoryReset();

    // Nota de "hay un OTA en curso" que sobrevive al reboot que dispara un
    // flasheo con exito, para poder mostrar el resultado en el portal la
    // proxima vez que arranca (ver AppStateMachine y OtaUpdater).
    void markOtaPending(const String& fromVersion, const String& toVersion);
    void clearOtaNote();
    bool takeOtaNote(String& fromVersion, String& toVersion);

    // UID de la tarjeta NFC aprendida (fuera de DeviceConfig, igual que la nota
    // de OTA: no requiere reboot para aplicarse, a diferencia del resto de
    // config). Cacheado en RAM (no "consume-al-leer" como takeOtaNote(), hace
    // falta en cada tap de tarjeta).
    bool hasLearnedCard() const;
    String learnedCardUid() const;
    void setLearnedCardUid(const String& uidHex);

private:
    DeviceConfig current_;
    String learnedCardUid_;
};
