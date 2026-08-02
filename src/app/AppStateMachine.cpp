#include "AppStateMachine.h"

#include <time.h>

#include "Log.h"
#include "Version.h"

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint32_t kPortalWaitMs = 10000; // espera inicial sin nadie conectado; una vez conectado no hay límite hasta que se desconecta
constexpr uint32_t kNtpSyncTimeoutMs = 15000; // aviso si no ha sincronizado NTP en este tiempo desde que hay WiFi

LedCommand ledSlowBlink(LedColor color) {
    LedCommand cmd;
    cmd.color = color;
    cmd.mode = LedMode::BLINK_SLOW;
    return cmd;
}

LedCommand ledOff() {
    LedCommand cmd;
    cmd.color = LedColor::OFF;
    cmd.mode = LedMode::OFF;
    return cmd;
}

LedCommand ledSolid(LedColor color) {
    LedCommand cmd;
    cmd.color = color;
    cmd.mode = LedMode::SOLID;
    return cmd;
}

LedCommand ledAllSolid() {
    return ledSolid(LedColor::ALL);
}

LedCommand ledForStatus(WoffuStatus status) {
    switch (status) {
        case WoffuStatus::CLOCKED_IN:
            return ledSolid(LedColor::GREEN);
        case WoffuStatus::CLOCKED_OUT:
            return ledSolid(LedColor::RED);
        default:
            return ledSolid(LedColor::YELLOW);
    }
}

const char* pollModeName(PollMode mode) {
    switch (mode) {
        case PollMode::ACTIVE:
            return "activa";
        case PollMode::PASSIVE:
            return "pasiva";
        default:
            return "off";
    }
}

const char* woffuStatusName(WoffuStatus status) {
    switch (status) {
        case WoffuStatus::CLOCKED_IN:
            return "FICHADO (verde)";
        case WoffuStatus::CLOCKED_OUT:
            return "NO FICHADO (rojo)";
        default:
            return "DESCONOCIDO (ambar)";
    }
}
}

void AppStateMachine::begin() {
    config_.begin();
    led_.begin(kPinLedRed, kPinLedYellow, kPinLedGreen);

    otaUpdater_.setBeforeFlashCallback([this](const String& from, const String& to) {
        config_.markOtaPending(from, to);
    });

    String otaFrom, otaTo;
    if (config_.takeOtaNote(otaFrom, otaTo)) {
        if (otaTo == FIRMWARE_VERSION) {
            portal_.reportOtaStatus("OTA: actualizado correctamente de " + otaFrom + " a " + otaTo + ".");
        } else {
            portal_.reportOtaStatus("OTA: se intento actualizar a " + otaTo +
                                     " pero el dispositivo arranco con " + String(FIRMWARE_VERSION) +
                                     " - revisa el log serie.");
        }
    }

    state_ = config_.get().configured ? AppState::PORTAL_WINDOW : AppState::UNCONFIGURED;

    switch (state_) {
        case AppState::UNCONFIGURED:
            enterUnconfigured();
            break;
        case AppState::PORTAL_WINDOW:
            enterPortalWindow();
            break;
        default:
            break;
    }
}

void AppStateMachine::loop() {
    if (state_ != AppState::RUNNING) {
        handlePortal();
    }

    bool wifiConnected = wifi_.isConnected();
    if (wifiConnected && !wifiWasConnected_) {
        logPrintf("WiFi conectado, IP: %s. Detectando zona horaria y sincronizando hora por NTP...\n",
                  wifi_.ipAddress().c_str());
        timeSync_.begin();
        wifiConnectedAtMs_ = millis();
        timeSyncTimeoutLogged_ = false;
    }
    wifiWasConnected_ = wifiConnected;

    // Solo se loguea la primera sincronizacion (el reloj no se "des-sincroniza" solo:
    // NTP resincroniza en segundo plano cada pocas horas para corregir deriva, sin
    // que haga falta reflejarlo aqui) y un aviso si no llega a sincronizar nunca.
    bool timeSynced = timeSync_.isSynced();
    if (timeSynced && !timeWasSynced_) {
        logPrintln("Hora sincronizada por NTP.");
    } else if (!timeSynced && wifiConnected && !timeSyncTimeoutLogged_ &&
               millis() - wifiConnectedAtMs_ > kNtpSyncTimeoutMs) {
        logPrintln("Aviso: no se ha podido sincronizar la hora por NTP (timeout).");
        timeSyncTimeoutLogged_ = true;
    }
    timeWasSynced_ = timeSynced;

    if (state_ == AppState::PORTAL_WINDOW && !portalClientConnected_ && millis() >= portalWindowDeadlineMs_) {
        logPrintln("Ventana de portal cerrada: nadie se conecto en 10s. Pasando a modo normal.");
        enterRunning();
    }

    if (state_ == AppState::RUNNING) {
        handleRunning();
    }
}

void AppStateMachine::enterUnconfigured() {
    logPrintln("Dispositivo sin configurar: portal de configuracion activo indefinidamente.");
    portal_.begin(config_.get());
    updateLedForCurrentState();
}

void AppStateMachine::enterPortalWindow() {
    // AP (portal) + STA (WiFi real) en paralelo (WIFI_MODE_APSTA): sin la
    // STA no hay salida a internet mientras el portal esta abierto, y el
    // boton de OTA del propio portal la necesita para llegar a GitHub.
    logPrintln("Ventana de portal de configuracion abierta (10s, o hasta que se desconecte el cliente).");
    portal_.begin(config_.get());
    wifi_.begin(config_.get().wifiSsid, config_.get().wifiPassword);
    portalWindowDeadlineMs_ = millis() + kPortalWaitMs;
    updateLedForCurrentState();
}

void AppStateMachine::enterRunning() {
    state_ = AppState::RUNNING;
    logPrintln("Modo normal (RUNNING): portal apagado, conectando a la WiFi configurada.");
    portal_.stop();
    if (!wifi_.isConnected()) {
        // Puede que ya este conectada desde PORTAL_WINDOW; evitar reconectar
        // innecesariamente a la misma red.
        wifi_.begin(config_.get().wifiSsid, config_.get().wifiPassword);
    }
    scheduler_.configure(config_.get());
    woffuClient_.begin(config_.get().woffuUsername, config_.get().woffuPassword);
    led_.set(ledOff());
}

void AppStateMachine::handlePortal() {
    portal_.loop();

    bool connected = portal_.hasClient();
    if (connected != portalClientConnected_) {
        portalClientConnected_ = connected;
        updateLedForCurrentState();
        if (connected) {
            logPrintln("Cliente conectado al portal de configuracion.");
        } else {
            logPrintln("Cliente desconectado del portal de configuracion.");
            if (state_ == AppState::PORTAL_WINDOW) {
                // Se acaba de desconectar el último cliente: cerramos ya la ventana.
                logPrintln("Ventana de portal cerrada: se desconecto el ultimo cliente. Pasando a modo normal.");
                enterRunning();
                return;
            }
        }
    }

    DeviceConfig newConfig;
    if (portal_.takeConfigToSave(newConfig)) {
        logPrintln("Nueva configuracion guardada desde el portal. Reiniciando...");
        saveConfigAndReboot(newConfig);
        return;
    }

    if (portal_.takeOtaRequested()) {
        logPrintln("Actualizacion OTA solicitada desde el portal. Comprobando...");
        if (!wifi_.isConnected()) {
            logPrintln("OTA: todavia sin conexion a la WiFi configurada.");
            portal_.reportOtaStatus(
                "OTA: el dispositivo todavia no tiene conexion a la WiFi configurada "
                "(necesaria para llegar a GitHub). Espera unos segundos y vuelve a intentarlo.");
            return;
        }
        switch (otaUpdater_.checkAndUpdate()) {
            case OtaResult::UP_TO_DATE:
                logPrintln("OTA: ya esta en la ultima version.");
                portal_.reportOtaStatus("OTA: ya tienes la ultima version instalada.");
                break;
            case OtaResult::UPDATED:
                // httpUpdate.rebootOnUpdate(true) reinicia dentro de checkAndUpdate() en
                // caso de exito: si llegamos aqui es que, excepcionalmente, no reinicio.
                logPrintln("OTA: actualizado correctamente, reiniciando...");
                portal_.reportOtaStatus("OTA: actualizado correctamente, reiniciando...");
                break;
            case OtaResult::ERROR:
                // Si el fallo fue en el flasheo (tras el callback de OtaUpdater), no hubo
                // reboot: limpiar la nota para que no reaparezca en un reinicio posterior
                // sin relacion (p.ej. un guardado normal de config dias despues).
                config_.clearOtaNote();
                logPrintf("OTA: error comprobando o descargando la actualizacion (%s).\n",
                          otaUpdater_.lastErrorDetail().c_str());
                portal_.reportOtaStatus("OTA: error - " + otaUpdater_.lastErrorDetail());
                break;
        }
    }

    if (portal_.takeFactoryResetRequested()) {
        logPrintln("Reset de fabrica solicitado desde el portal. Borrando configuracion y reiniciando...");
        config_.factoryReset();
        delay(300);
        ESP.restart();
    }
}

void AppStateMachine::handleRunning() {
    if (!wifi_.isConnected() || !timeSync_.isSynced()) {
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    uint16_t nowMinutes = static_cast<uint16_t>(timeinfo.tm_hour * 60 + timeinfo.tm_min);

    const DeviceConfig& cfg = config_.get();
    bool withinOnOffWindow =
        nowMinutes >= cfg.activeWindow.startMinutes && nowMinutes < cfg.activeWindow.endMinutes;
    // Se pide la jornada del dia a Woffu (login + users + workday) una vez al dia: mientras
    // estamos dentro de la ventana de encendido (fuera de ella no hace falta ni saberlo, ya
    // esta apagado), o siempre que este forzada la ventana activa (para poder probar el
    // flujo completo sin esperar al horario real; el resultado no cambia el modo forzado,
    // solo queda constancia en el log).
    if ((cfg.forceActiveWindow || withinOnOffWindow) && timeinfo.tm_yday != lastWorkdayYday_) {
        refreshWorkdayInfo(timeinfo.tm_yday);
    }

    String reason;
    PollMode mode = scheduler_.currentMode(nowMinutes, workdayInfo_, workdayValid_, reason);
    if (!runningPollModeKnown_ || mode != runningPollMode_) {
        runningPollMode_ = mode;
        runningPollModeKnown_ = true;
        nextPollAtMs_ = millis(); // fuerza un poll inmediato al entrar en una ventana nueva
        logPrintf("Cambio de ventana de polling: %s (%s)\n", pollModeName(mode), reason.c_str());
    }

    if (mode == PollMode::OFF) {
        led_.set(ledOff());
        return;
    }

    if (millis() < nextPollAtMs_) {
        return;
    }
    nextPollAtMs_ = millis() + static_cast<uint32_t>(scheduler_.pollIntervalSeconds(mode)) * 1000UL;

    WoffuStatus status = woffuClient_.fetchStatus();
    logPrintf("Estado Woffu: %s\n", woffuStatusName(status));
    led_.set(ledForStatus(status));
}

void AppStateMachine::refreshWorkdayInfo(int yday) {
    // Se marca como "intentado hoy" tanto en exito como en fallo: si falla no se
    // reintenta hasta manana (mismo espiritu de "sin reintentos adicionales" que
    // ya aplica a los fallos de fetchStatus(), ver Requisitos.md).
    lastWorkdayYday_ = yday;

    WorkdayInfo info;
    if (woffuClient_.fetchWorkday(info)) {
        workdayInfo_ = info;
        workdayValid_ = true;
        logPrintf(
            "Woffu: jornada de hoy - ventana pasiva %02u:%02u-%02u:%02u, fin de semana=%s, festivo=%s.\n",
            info.startMinutes / 60, info.startMinutes % 60, info.endMinutes / 60, info.endMinutes % 60,
            info.isWeekend ? "si" : "no", info.isHoliday ? "si" : "no");
    } else {
        workdayValid_ = false;
        logPrintln(
            "Woffu: no se pudo obtener la jornada de hoy; se usara polling activo por defecto hasta manana.");
    }
}

void AppStateMachine::saveConfigAndReboot(const DeviceConfig& newConfig) {
    config_.save(newConfig);
    delay(300);
    ESP.restart();
}

void AppStateMachine::updateLedForCurrentState() {
    if (state_ != AppState::UNCONFIGURED && state_ != AppState::PORTAL_WINDOW) {
        return;
    }
    led_.set(portalClientConnected_ ? ledAllSolid() : ledSlowBlink(LedColor::ALL));
}
