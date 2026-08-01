#include "AppStateMachine.h"

#include <time.h>

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint32_t kPortalWaitMs = 10000; // espera inicial sin nadie conectado; una vez conectado no hay límite hasta que se desconecta
constexpr uint32_t kNtpSyncTimeoutMs = 15000; // aviso si no ha sincronizado NTP en este tiempo desde que hay WiFi

LedCommand ledSlowBlink(LedColor color, uint8_t brightness) {
    LedCommand cmd;
    cmd.color = color;
    cmd.mode = LedMode::BLINK_SLOW;
    cmd.brightness = brightness;
    return cmd;
}

LedCommand ledOff() {
    LedCommand cmd;
    cmd.color = LedColor::OFF;
    cmd.mode = LedMode::OFF;
    return cmd;
}

LedCommand ledSolid(LedColor color, uint8_t brightness) {
    LedCommand cmd;
    cmd.color = color;
    cmd.mode = LedMode::SOLID;
    cmd.brightness = brightness;
    return cmd;
}

LedCommand ledAllSolid(uint8_t brightness) {
    return ledSolid(LedColor::ALL, brightness);
}

LedCommand ledForStatus(WoffuStatus status, uint8_t brightness) {
    switch (status) {
        case WoffuStatus::CLOCKED_IN:
            return ledSolid(LedColor::GREEN, brightness);
        case WoffuStatus::CLOCKED_OUT:
            return ledSolid(LedColor::RED, brightness);
        default:
            return ledSolid(LedColor::YELLOW, brightness);
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
        Serial.printf("WiFi conectado, IP: %s. Sincronizando hora por NTP...\n", wifi_.ipAddress().c_str());
        timeSync_.begin(config_.get().timezone);
        wifiConnectedAtMs_ = millis();
        timeSyncTimeoutLogged_ = false;
    }
    wifiWasConnected_ = wifiConnected;

    // Solo se loguea la primera sincronizacion (el reloj no se "des-sincroniza" solo:
    // NTP resincroniza en segundo plano cada pocas horas para corregir deriva, sin
    // que haga falta reflejarlo aqui) y un aviso si no llega a sincronizar nunca.
    bool timeSynced = timeSync_.isSynced();
    if (timeSynced && !timeWasSynced_) {
        Serial.println("Hora sincronizada por NTP.");
    } else if (!timeSynced && wifiConnected && !timeSyncTimeoutLogged_ &&
               millis() - wifiConnectedAtMs_ > kNtpSyncTimeoutMs) {
        Serial.println("Aviso: no se ha podido sincronizar la hora por NTP (timeout).");
        timeSyncTimeoutLogged_ = true;
    }
    timeWasSynced_ = timeSynced;

    if (state_ == AppState::PORTAL_WINDOW && !portalClientConnected_ && millis() >= portalWindowDeadlineMs_) {
        Serial.println("Ventana de portal cerrada: nadie se conecto en 10s. Pasando a modo normal.");
        enterRunning();
    }

    if (state_ == AppState::RUNNING) {
        handleRunning();
    }
}

void AppStateMachine::enterUnconfigured() {
    Serial.println("Dispositivo sin configurar: portal de configuracion activo indefinidamente.");
    portal_.begin(config_.get());
    updateLedForCurrentState();
}

void AppStateMachine::enterPortalWindow() {
    // AP (portal) + STA (WiFi real) en paralelo (WIFI_MODE_APSTA): sin la
    // STA no hay salida a internet mientras el portal esta abierto, y el
    // boton de OTA del propio portal la necesita para llegar a GitHub.
    Serial.println("Ventana de portal de configuracion abierta (10s, o hasta que se desconecte el cliente).");
    portal_.begin(config_.get());
    wifi_.begin(config_.get().wifiSsid, config_.get().wifiPassword);
    portalWindowDeadlineMs_ = millis() + kPortalWaitMs;
    updateLedForCurrentState();
}

void AppStateMachine::enterRunning() {
    state_ = AppState::RUNNING;
    Serial.println("Modo normal (RUNNING): portal apagado, conectando a la WiFi configurada.");
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
            Serial.println("Cliente conectado al portal de configuracion.");
        } else {
            Serial.println("Cliente desconectado del portal de configuracion.");
            if (state_ == AppState::PORTAL_WINDOW) {
                // Se acaba de desconectar el último cliente: cerramos ya la ventana.
                Serial.println("Ventana de portal cerrada: se desconecto el ultimo cliente. Pasando a modo normal.");
                enterRunning();
                return;
            }
        }
    }

    DeviceConfig newConfig;
    if (portal_.takeConfigToSave(newConfig)) {
        Serial.println("Nueva configuracion guardada desde el portal. Reiniciando...");
        saveConfigAndReboot(newConfig);
        return;
    }

    if (portal_.takeOtaRequested()) {
        Serial.println("Actualizacion OTA solicitada desde el portal. Comprobando...");
        if (!wifi_.isConnected()) {
            Serial.println("OTA: todavia sin conexion a la WiFi configurada.");
            portal_.reportOtaStatus(
                "OTA: el dispositivo todavia no tiene conexion a la WiFi configurada "
                "(necesaria para llegar a GitHub). Espera unos segundos y vuelve a intentarlo.");
            return;
        }
        switch (otaUpdater_.checkAndUpdate()) {
            case OtaResult::UP_TO_DATE:
                Serial.println("OTA: ya esta en la ultima version.");
                portal_.reportOtaStatus("OTA: ya tienes la ultima version instalada.");
                break;
            case OtaResult::UPDATED:
                // httpUpdate.rebootOnUpdate(true) reinicia dentro de checkAndUpdate() en
                // caso de exito: si llegamos aqui es que, excepcionalmente, no reinicio.
                Serial.println("OTA: actualizado correctamente, reiniciando...");
                portal_.reportOtaStatus("OTA: actualizado correctamente, reiniciando...");
                break;
            case OtaResult::ERROR:
                Serial.printf("OTA: error comprobando o descargando la actualizacion (%s).\n",
                               otaUpdater_.lastErrorDetail().c_str());
                portal_.reportOtaStatus("OTA: error - " + otaUpdater_.lastErrorDetail());
                break;
        }
    }

    if (portal_.takeFactoryResetRequested()) {
        Serial.println("Reset de fabrica solicitado desde el portal. Borrando configuracion y reiniciando...");
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
    uint8_t isoWeekday = timeinfo.tm_wday == 0 ? 7 : static_cast<uint8_t>(timeinfo.tm_wday);

    PollMode mode = scheduler_.currentMode(nowMinutes, isoWeekday);
    if (!runningPollModeKnown_ || mode != runningPollMode_) {
        runningPollMode_ = mode;
        runningPollModeKnown_ = true;
        nextPollAtMs_ = millis(); // fuerza un poll inmediato al entrar en una ventana nueva
        Serial.printf("Cambio de ventana de polling: %s\n", pollModeName(mode));
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
    Serial.printf("Estado Woffu: %s\n", woffuStatusName(status));
    led_.set(ledForStatus(status, config_.get().brightness));
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
    uint8_t brightness = config_.get().brightness;
    led_.set(portalClientConnected_ ? ledAllSolid(brightness) : ledSlowBlink(LedColor::ALL, brightness));
}
