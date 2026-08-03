#include "AppStateMachine.h"

#include <time.h>

#include "Log.h"
#include "Version.h"

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint8_t kPinNfcCs = 5; // VSPI: SCK=18, MISO=19, MOSI=23 (bus SPI global), CS dedicado en GPIO5
constexpr uint32_t kPortalWaitMs = 15000; // espera inicial sin nadie conectado; una vez conectado no hay límite hasta que se desconecta
constexpr uint32_t kNtpSyncTimeoutMs = 15000; // aviso si no ha sincronizado NTP en este tiempo desde que hay WiFi
constexpr uint32_t kWifiConnectTimeoutMs = 20000; // error si no conecta a la WiFi configurada en este tiempo desde RUNNING
constexpr uint32_t kConnectingTimeoutMs = 20000; // tiempo maximo en CONNECTING antes de abrir el portal igualmente
constexpr uint32_t kNfcLearnTimeoutMs = 30000; // tiempo maximo esperando una tarjeta durante el aprendizaje
constexpr uint32_t kNfcLearnResultHoldMs = 3000; // cuanto se mantiene el "parpadeo ALL" tras aprender una tarjeta
constexpr uint32_t kNfcResultHoldMs = 3000; // cuanto se mantiene el rojo/verde/ambar tras un tap en RUNNING

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

LedCommand ledRotate() {
    LedCommand cmd;
    cmd.color = LedColor::ALL;  // ignorado en modo ROTATE, ver LedController
    cmd.mode = LedMode::ROTATE;
    return cmd;
}

LedCommand ledRotateFast() {
    LedCommand cmd;
    cmd.color = LedColor::ALL;  // ignorado en modo ROTATE_FAST, ver LedController
    cmd.mode = LedMode::ROTATE_FAST;
    return cmd;
}

LedCommand ledFastBlink(LedColor color) {
    LedCommand cmd;
    cmd.color = color;
    cmd.mode = LedMode::BLINK_FAST;
    return cmd;
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
    // Fallo no fatal: el dispositivo sigue funcionando sin fichaje por NFC si
    // el lector no responde (cableado incorrecto, modulo en otro modo...).
    nfcReader_.begin(kPinNfcCs);
    // Rotando desde el primer instante: portal_.begin() (mas abajo, via
    // enterPortalWindow()) incluye un escaneo de redes WiFi que ya tarda unos
    // segundos por si solo, y sin esto los LEDs se quedarian apagados durante
    // ese hueco en vez de mostrar que el dispositivo esta "cargando".
    // updateLedForCurrentState() lo sustituye por el patron definitivo en
    // cuanto el portal esta realmente listo.
    led_.set(ledRotate());

    otaUpdater_.setBeforeFlashCallback([this](const String& from, const String& to) {
        config_.markOtaPending(from, to);
    });
    // El portal esta bloqueado (no llama a WebServer::handleClient()) mientras
    // dura la descarga: este callback "bombea" el portal desde dentro de la
    // propia llamada bloqueante para que la pagina siga respondiendo peticiones
    // (el meta-refresh de ProvisioningPortal) y muestre progreso real.
    otaUpdater_.setProgressCallback([this](size_t current, size_t total) {
        portal_.reportOtaProgress(current, total);
        // Solo se loguea cada 10% (y el ultimo tramo): a cada sector de 4KB
        // escrito en flash saldrian decenas de lineas por segundo.
        int percent = total > 0 ? static_cast<int>((current * 100) / total) : 0;
        if (percent != lastLoggedOtaPercent_ && (percent % 10 == 0 || percent >= 100)) {
            lastLoggedOtaPercent_ = percent;
            if (percent >= 100) {
                logPrintln("OTA: descarga completa, finalizando instalacion...");
            } else {
                logPrintf("OTA: descargando firmware... %d%%\n", percent);
            }
        }
        portal_.loop();
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

    // Se intenta conectar siempre, este o no configurado el dispositivo: sin
    // SSID guardado, wifi_.begin() simplemente nunca llegara a conectar y
    // handleConnecting() acabara abriendo el portal igualmente por timeout,
    // que es exactamente el mismo desenlace que si la WiFi guardada fuera
    // invalida. No hace falta distinguir ambos casos.
    state_ = AppState::CONNECTING;
    enterConnecting();
}

void AppStateMachine::loop() {
    if (state_ == AppState::PORTAL_WINDOW) {
        handlePortal();
    }

    bool wifiConnected = wifi_.isConnected();
    if (wifiConnected && !wifiWasConnected_) {
        logPrintf("WiFi conectado, IP: %s. Detectando zona horaria y sincronizando hora por NTP...\n",
                  wifi_.ipAddress().c_str());
        timeSync_.begin();
        wifiConnectedAtMs_ = millis();
        timeSyncTimeoutLogged_ = false;
    } else if (!wifiConnected && wifiWasConnected_) {
        wifiDisconnectedSinceMs_ = millis();
        wifiConnectTimeoutLogged_ = false;
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

    if (state_ == AppState::CONNECTING) {
        handleConnecting();
    }

    // Si nunca se llego a conectar a la WiFi configurada, pasar a RUNNING no
    // tiene sentido (ahi solo se acabaria parpadeando en ambar con el portal
    // ya cerrado, sin forma de reconfigurar el dispositivo): la ventana solo
    // se cierra sola por timeout cuando hay conexion WiFi real.
    if (state_ == AppState::PORTAL_WINDOW && !portalClientConnected_ && wifi_.isConnected() &&
        millis() >= portalWindowDeadlineMs_) {
        logPrintln("Ventana de portal cerrada: nadie se conecto en 15s. Pasando a modo normal.");
        enterRunning();
    }

    if (state_ == AppState::RUNNING) {
        handleRunning();
    }
}

void AppStateMachine::enterConnecting() {
    // Antes de abrir el portal, se intenta conectar a la WiFi guardada,
    // sincronizar la hora y comprobar actualizaciones OTA (ver
    // handleConnecting()): si todo va bien, el portal se abre ya con el
    // resultado listo, sin depender de que la pagina se auto-refresque
    // mientras el usuario puede estar rellenando el formulario (ver
    // ProvisioningPortal). Si no hay wifi o tarda demasiado (credenciales
    // cambiadas, red no disponible...), se abre el portal igualmente pasado
    // kConnectingTimeoutMs para no dejar el dispositivo sin forma de
    // reconfigurarse.
    logPrintln("Conectando a la WiFi configurada y sincronizando hora antes de abrir el portal...");
    wifi_.begin(config_.get().wifiSsid, config_.get().wifiPassword);
    connectingDeadlineMs_ = millis() + kConnectingTimeoutMs;
    led_.set(ledRotate());
}

void AppStateMachine::handleConnecting() {
    if (wifi_.isConnected() && timeSync_.isSynced()) {
        otaCheckTriggered_ = true;
        performOtaCheck();
        enterPortalWindow();
        return;
    }
    if (millis() >= connectingDeadlineMs_) {
        logPrintln("Conectando: tiempo de espera agotado (WiFi o NTP); se abre el portal igualmente.");
        enterPortalWindow();
    }
}

void AppStateMachine::enterPortalWindow() {
    // AP (portal) + STA (WiFi real) en paralelo (WIFI_MODE_APSTA, ya arrancada
    // en enterConnecting()): sin la STA no hay salida a internet mientras el
    // portal esta abierto, y la comprobacion/actualizacion OTA del propio
    // portal la necesita para llegar a GitHub.
    state_ = AppState::PORTAL_WINDOW;
    logPrintln("Ventana de portal de configuracion abierta (15s si hay WiFi, o hasta que se desconecte el "
               "cliente; indefinida mientras no haya conexion WiFi).");
    portal_.begin(config_.get(), config_.hasLearnedCard());
    portalWindowDeadlineMs_ = millis() + kPortalWaitMs;
    nfcLearnActive_ = false;
    nfcLearnResultUntilMs_ = 0;
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
    wifiDisconnectedSinceMs_ = millis();
    wifiConnectTimeoutLogged_ = false;
    // Rotando hasta que handleRunning() tenga un estado real que mostrar
    // (reconectar wifi, sincronizar NTP y la primera consulta a Woffu pueden
    // tardar unos segundos): dejarlo en OFF aqui seria indistinguible del OFF
    // legitimo por estar fuera de horario/fin de semana.
    led_.set(ledRotate());
}

void AppStateMachine::handlePortal() {
    // Comprobacion OTA automatica, una sola vez por arranque: hace falta wifi
    // Y hora sincronizada (sin NTP el reloj esta a 1970 y la validacion del
    // certificado TLS de GitHub falla como "no valido todavia" - mismo motivo
    // por el que handleRunning() espera a timeSync_.isSynced() antes de llamar
    // a Woffu). No depende de que nadie haya abierto la pagina del portal.
    if (!otaCheckTriggered_ && wifi_.isConnected() && timeSync_.isSynced()) {
        otaCheckTriggered_ = true;
        performOtaCheck();
    }

    portal_.loop();

    bool connected = portal_.hasClient();
    if (connected != portalClientConnected_) {
        portalClientConnected_ = connected;
        updateLedForCurrentState();
        if (connected) {
            logPrintln("Cliente conectado al portal de configuracion.");
        } else {
            logPrintln("Cliente desconectado del portal de configuracion.");
            // Solo cerramos ya la ventana si hay conexion WiFi real: pasar a RUNNING
            // sin ella dejaria el dispositivo sin WiFi ni portal, sin forma de
            // reconfigurarse (ver comentario en loop()).
            if (state_ == AppState::PORTAL_WINDOW && wifi_.isConnected()) {
                logPrintln("Ventana de portal cerrada: se desconecto el ultimo cliente. Pasando a modo normal.");
                enterRunning();
                return;
            }
        }
    }

    if (!nfcLearnActive_ && portal_.takeNfcLearnRequested()) {
        nfcLearnActive_ = true;
        nfcLearnDeadlineMs_ = millis() + kNfcLearnTimeoutMs;
        logPrintln("NFC: aprendizaje iniciado desde el portal, esperando tarjeta...");
        led_.set(ledRotateFast());
    }
    if (nfcLearnActive_) {
        String uid;
        if (nfcReader_.poll(uid)) {
            config_.setLearnedCardUid(uid);
            logPrintf("NFC: tarjeta aprendida (UID %s).\n", uid.c_str());
            portal_.reportNfcLearnSuccess(uid);
            led_.set(ledFastBlink(LedColor::ALL));
            nfcLearnActive_ = false;
            nfcLearnResultUntilMs_ = millis() + kNfcLearnResultHoldMs;
        } else if (millis() >= nfcLearnDeadlineMs_) {
            logPrintln("NFC: aprendizaje cancelado (no se detecto ninguna tarjeta a tiempo).");
            portal_.reportNfcLearnTimeout();
            nfcLearnActive_ = false;
            updateLedForCurrentState();
        }
    }
    if (nfcLearnResultUntilMs_ != 0 && millis() >= nfcLearnResultUntilMs_) {
        nfcLearnResultUntilMs_ = 0;
        updateLedForCurrentState();
    }

    DeviceConfig newConfig;
    if (portal_.takeConfigToSave(newConfig)) {
        logPrintln("Nueva configuracion guardada desde el portal. Reiniciando...");
        saveConfigAndReboot(newConfig);
        return;
    }

    if (portal_.takeOtaUpdateRequested()) {
        logPrintln("Actualizacion OTA solicitada desde el portal. Descargando e instalando...");
        if (!wifi_.isConnected()) {
            logPrintln("OTA: todavia sin conexion a la WiFi configurada.");
            portal_.reportOtaError(
                "el dispositivo todavia no tiene conexion a la WiFi configurada (necesaria "
                "para llegar a GitHub). Espera unos segundos y vuelve a intentarlo.");
        } else {
            lastLoggedOtaPercent_ = -1;
            // handleOtaUpdate() ya respondio el 302 al POST y puso el estado en
            // UPDATING antes de que llegaramos aqui, pero el navegador todavia
            // no ha tenido tiempo de hacer el GET / que sigue a ese redirect.
            // otaUpdater_.update() no vuelve a ceder el turno hasta el primer
            // callback de progreso (que puede tardar varios segundos: conexion
            // TLS + posibles redirecciones de GitHub antes de empezar a
            // descargar), asi que sin este bombeo previo el navegador se
            // quedaria varios segundos sin respuesta justo tras pulsar
            // "Actualizar" - tiempo de sobra para que alguien impaciente le
            // de otra vez al boton o refresque a mano.
            uint32_t pumpUntilMs = millis() + 800;
            while (millis() < pumpUntilMs) {
                portal_.loop();
                delay(10);
            }
            switch (otaUpdater_.update(portal_.otaTargetVersion())) {
                case OtaResult::UP_TO_DATE:
                    logPrintln("OTA: ya esta en la ultima version.");
                    portal_.reportOtaUpToDate();
                    break;
                case OtaResult::UPDATED:
                    // httpUpdate.rebootOnUpdate(true) reinicia dentro de update() en caso de
                    // exito: si llegamos aqui es que, excepcionalmente, no reinicio.
                    logPrintln("OTA: actualizado correctamente, reiniciando...");
                    portal_.reportOtaProgress(1, 1);
                    break;
                case OtaResult::ERROR:
                    // Si el fallo fue en el flasheo (tras el callback de OtaUpdater), no hubo
                    // reboot: limpiar la nota para que no reaparezca en un reinicio posterior
                    // sin relacion (p.ej. un guardado normal de config dias despues).
                    config_.clearOtaNote();
                    logPrintf("OTA: error descargando/instalando la actualizacion (%s).\n",
                              otaUpdater_.lastErrorDetail().c_str());
                    portal_.reportOtaError(otaUpdater_.lastErrorDetail());
                    break;
            }
        }
    }

    if (portal_.takeFactoryResetRequested()) {
        logPrintln("Reset de fabrica solicitado desde el portal. Borrando configuracion y reiniciando...");
        config_.factoryReset();
        delay(300);
        ESP.restart();
    }
}

void AppStateMachine::performOtaCheck() {
    logPrintln("Comprobacion automatica de actualizacion OTA (wifi y hora listas). Comprobando...");
    portal_.reportOtaChecking();
    String latest;
    switch (otaUpdater_.checkForUpdate(latest)) {
        case OtaCheckResult::UP_TO_DATE:
            logPrintln("OTA: ya esta en la ultima version.");
            portal_.reportOtaUpToDate();
            break;
        case OtaCheckResult::AVAILABLE:
            logPrintf("OTA: version nueva disponible (%s -> %s).\n", FIRMWARE_VERSION, latest.c_str());
            portal_.reportOtaAvailable(latest);
            break;
        case OtaCheckResult::ERROR:
            logPrintf("OTA: error comprobando actualizacion (%s).\n", otaUpdater_.lastErrorDetail().c_str());
            portal_.reportOtaError(otaUpdater_.lastErrorDetail());
            break;
    }
}

void AppStateMachine::handleRunning() {
    if (!wifi_.isConnected()) {
        if (!wifiConnectTimeoutLogged_ && millis() - wifiDisconnectedSinceMs_ > kWifiConnectTimeoutMs) {
            logPrintln("Error: no se ha podido conectar a la WiFi configurada (revisa SSID/password en el portal).");
            wifiConnectTimeoutLogged_ = true;
            // Un unico led_.set(): LedController resetea la fase del parpadeo cada vez que
            // recibe un comando (xQueueOverwrite), asi que reenviarlo en cada vuelta de loop()
            // (mucho mas rapido que el periodo de parpadeo) lo dejaria fijo en vez de parpadeando.
            led_.set(ledSlowBlink(LedColor::YELLOW));
        }
        return;
    }
    if (!timeSync_.isSynced()) {
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
    PollMode mode = scheduler_.currentMode(nowMinutes, workdayInfo_, workdayValid_, lastWoffuStatus_, reason);
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

    // NFC: escucha continuamente en cada tick, independiente del throttle de
    // polling HTTP de mas abajo, y solo dentro de PollMode::ACTIVE (fuera del
    // tramo de "ventana pasiva" que reporta Woffu).
    if (mode == PollMode::ACTIVE) {
        if (nfcSignState_ == NfcSignState::RESULT) {
            if (millis() < nfcSignResultUntilMs_) {
                return;  // se sigue mostrando el resultado, no lo pisa el color normal de abajo
            }
            nfcSignState_ = NfcSignState::IDLE;
        }

        String uid;
        if (nfcReader_.poll(uid)) {
            logPrintf("NFC: tarjeta detectada (UID %s).\n", uid.c_str());
            led_.set(ledRotateFast());

            if (config_.hasLearnedCard() && uid == config_.learnedCardUid()) {
                const char* accion = (lastWoffuStatus_ == WoffuStatus::CLOCKED_IN) ? "salida" : "entrada";
                logPrintf("NFC: UID coincide con la tarjeta aprendida. Fichando %s...\n", accion);
                if (woffuClient_.toggleSign()) {
                    logPrintf("Woffu API: fichaje de %s registrado por NFC.\n", accion);
                    led_.set(ledFastBlink(LedColor::GREEN));
                } else {
                    logPrintln("Woffu API: error al registrar el fichaje por NFC.");
                    led_.set(ledFastBlink(LedColor::YELLOW));
                }
                // Fuerza un repoll real tras la ventana de resultado, para que el
                // color definitivo del semaforo salga siempre de un fetchStatus()
                // real y no de una suposicion optimista (ver Woffu API arriba).
                nextPollAtMs_ = millis();
            } else {
                logPrintln("NFC: UID no coincide con la tarjeta aprendida.");
                led_.set(ledFastBlink(LedColor::RED));
            }

            nfcSignState_ = NfcSignState::RESULT;
            nfcSignResultUntilMs_ = millis() + kNfcResultHoldMs;
            return;
        }
    }

    if (millis() < nextPollAtMs_) {
        return;
    }
    nextPollAtMs_ = millis() + static_cast<uint32_t>(scheduler_.pollIntervalSeconds(mode)) * 1000UL;

    WoffuStatus status = woffuClient_.fetchStatus();
    lastWoffuStatus_ = status;
    logPrintf("Estado Woffu: %s\n", woffuStatusName(status));
    if (woffuClient_.credentialsInvalid()) {
        led_.set(ledSlowBlink(LedColor::YELLOW));
    } else {
        led_.set(ledForStatus(status));
    }
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
    if (state_ != AppState::PORTAL_WINDOW) {
        return;
    }
    led_.set(portalClientConnected_ ? ledAllSolid() : ledSlowBlink(LedColor::ALL));
}
