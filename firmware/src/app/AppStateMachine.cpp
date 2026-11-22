#include "AppStateMachine.h"

#include <WiFi.h>
#include <time.h>

#include "Log.h"
#include "Version.h"
#include "config/DeviceIdentity.h"

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint8_t kPinNfcCs = 5; // VSPI: SCK=18, MISO=19, MOSI=23 (bus SPI global), CS dedicado en GPIO5
constexpr uint32_t kPortalWaitMs = 30000; // espera inicial sin nadie conectado; una vez conectado no hay límite hasta que se desconecta
constexpr uint32_t kNtpSyncTimeoutMs = 15000; // aviso si no ha sincronizado NTP en este tiempo desde que hay WiFi
constexpr uint32_t kWifiConnectTimeoutMs = 20000; // error si no conecta a la WiFi configurada en este tiempo desde RUNNING
constexpr uint32_t kConnectingTimeoutMs = 20000; // tiempo maximo en CONNECTING antes de abrir el portal igualmente
constexpr uint32_t kNfcLearnTimeoutMs = 30000; // tiempo maximo esperando una tarjeta durante el aprendizaje
constexpr uint32_t kNfcLearnResultHoldMs = 3000; // cuanto se mantiene el "parpadeo ALL" tras aprender una tarjeta
constexpr uint32_t kNfcResultHoldMs = 3000; // cuanto se mantiene el rojo/verde/ambar tras un tap en RUNNING
constexpr uint32_t kPollIntervalMs = 60000; // ritmo fijo de sondeo a Woffu mientras el dispositivo esta activo

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
            return "activo";
        default:
            return "off";
    }
}

const char* woffuStatusCode(WoffuStatus status) {
    switch (status) {
        case WoffuStatus::CLOCKED_IN:
            return "in";
        case WoffuStatus::CLOCKED_OUT:
            return "out";
        default:
            return "unknown";
    }
}

// Nombres de LedColor/LedMode en el protocolo del control remoto.
const char* const kLedColorNames[] = {"off", "red", "yellow", "green", "all"};
const char* const kLedModeNames[] = {"off", "solid", "blink_slow", "blink_fast", "rotate", "rotate_fast"};

const char* ledColorName(LedColor color) {
    uint8_t i = static_cast<uint8_t>(color);
    return i < sizeof(kLedColorNames) / sizeof(kLedColorNames[0]) ? kLedColorNames[i] : "?";
}

const char* ledModeName(LedMode mode) {
    uint8_t i = static_cast<uint8_t>(mode);
    return i < sizeof(kLedModeNames) / sizeof(kLedModeNames[0]) ? kLedModeNames[i] : "?";
}

// Solo se aceptan como color fijo remoto los modos "de reposo" (no los de
// rotacion, que se usan como indicador de "cargando/procesando").
bool parseOverride(const String& color, const String& mode, LedCommand& out) {
    bool colorOk = false;
    for (uint8_t i = 0; i < sizeof(kLedColorNames) / sizeof(kLedColorNames[0]); i++) {
        if (color == kLedColorNames[i]) {
            out.color = static_cast<LedColor>(i);
            colorOk = true;
        }
    }
    if (!colorOk) {
        return false;
    }
    if (out.color == LedColor::OFF) {
        out.mode = LedMode::OFF;
        return true;
    }
    if (mode == "" || mode == "solid") {
        out.mode = LedMode::SOLID;
    } else if (mode == "blink_slow") {
        out.mode = LedMode::BLINK_SLOW;
    } else if (mode == "blink_fast") {
        out.mode = LedMode::BLINK_FAST;
    } else {
        return false;
    }
    return true;
}

void ledToJson(JsonObject obj, const LedCommand& cmd) {
    obj["color"] = ledColorName(cmd.color);
    obj["mode"] = ledModeName(cmd.mode);
}

String minutesToHhMm(uint16_t minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", minutes / 60, minutes % 60);
    return String(buf);
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
    uint8_t overrideColor = 0;
    uint8_t overrideMode = 0;
    if (config_.ledOverride(overrideColor, overrideMode)) {
        overrideActive_ = true;
        override_.color = static_cast<LedColor>(overrideColor);
        override_.mode = static_cast<LedMode>(overrideMode);
        logPrintf("Semaforo: color fijo activo (%s, %s), puesto desde el control remoto.\n",
                  ledColorName(override_.color), ledModeName(override_.mode));
    }
    led_.begin(kPinLedRed, kPinLedYellow, kPinLedGreen);
    // Fallo no fatal: el dispositivo sigue funcionando sin fichaje por NFC si
    // el lector no responde (cableado incorrecto, modulo en otro modo...).
    nfcReaderOk_ = nfcReader_.begin(kPinNfcCs);
    // Rotando desde el primer instante: portal_.begin() (mas abajo, via
    // enterPortalWindow()) incluye un escaneo de redes WiFi que ya tarda unos
    // segundos por si solo, y sin esto los LEDs se quedarian apagados durante
    // ese hueco en vez de mostrar que el dispositivo esta "cargando".
    // updateLedForCurrentState() lo sustituye por el patron definitivo en
    // cuanto el portal esta realmente listo.
    setLed(ledRotate());

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
            if (state_ == AppState::RUNNING && percent % 20 == 0) {
                remote_.sendEvent("ota", "Descargando firmware... " + String(percent) + "%");
            }
        }
        // En RUNNING (OTA lanzada desde el control remoto) el portal esta
        // apagado, no hay nada que bombear.
        if (state_ == AppState::PORTAL_WINDOW) {
            portal_.loop();
        }
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
        logPrintln("Ventana de portal cerrada: nadie se conecto en 30s. Pasando a modo normal.");
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
    setLed(ledRotate());
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
    logPrintln("Ventana de portal de configuracion abierta (30s si hay WiFi, o hasta que se desconecte el "
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
    remote_.begin(config_.get().remoteUrl, DeviceIdentity::name(), DeviceIdentity::password());
    statusLedValid_ = false;
    wifiDisconnectedSinceMs_ = millis();
    wifiConnectTimeoutLogged_ = false;
    // Rotando hasta que handleRunning() tenga un estado real que mostrar
    // (reconectar wifi, sincronizar NTP y la primera consulta a Woffu pueden
    // tardar unos segundos): dejarlo en OFF aqui seria indistinguible del OFF
    // legitimo por estar fuera de horario/fin de semana.
    setLed(ledRotate());
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
        setLed(ledRotateFast());
    }
    if (nfcLearnActive_) {
        String uid;
        if (nfcReader_.poll(uid)) {
            config_.setLearnedCardUid(uid);
            logPrintf("NFC: tarjeta aprendida (UID %s).\n", uid.c_str());
            portal_.reportNfcLearnSuccess(uid);
            setLed(ledFastBlink(LedColor::ALL));
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
            // Un unico setLed(): LedController resetea la fase del parpadeo cada vez que
            // recibe un comando (xQueueOverwrite), asi que reenviarlo en cada vuelta de loop()
            // (mucho mas rapido que el periodo de parpadeo) lo dejaria fijo en vez de parpadeando.
            showStatusLed(ledSlowBlink(LedColor::YELLOW));
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
    // Se pide la jornada del dia a Woffu (login + users + workday) una vez al dia, mientras
    // estamos dentro de la ventana de encendido (fuera de ella no hace falta ni saberlo, ya
    // esta apagado).
    if (withinOnOffWindow && timeinfo.tm_yday != lastWorkdayYday_) {
        refreshWorkdayInfo(timeinfo.tm_yday);
    }

    String reason;
    PollMode mode = scheduler_.currentMode(nowMinutes, workdayInfo_, workdayValid_, reason);
    if (!runningPollModeKnown_ || mode != runningPollMode_) {
        runningPollMode_ = mode;
        runningPollModeKnown_ = true;
        pollModeReason_ = reason;
        nextPollAtMs_ = millis(); // fuerza un poll inmediato al entrar en una ventana nueva
        logPrintf("Cambio de ventana de polling: %s (%s)\n", pollModeName(mode), reason.c_str());
        remote_.requestStatus();
    }

    // Control remoto: tambien fuera de la ventana de encendido (p.ej. para
    // desfichar a distancia si se olvido al salir), por eso va antes del
    // return de PollMode::OFF.
    handleRemote(mode);

    if (mode == PollMode::OFF) {
        // El color fijo remoto no aplica aqui: fuera de horario el semaforo
        // esta apagado igualmente, sin estado que ocultar.
        statusLedValid_ = false;
        setLed(ledOff());
        return;
    }

    // Fichaje automatico: se comprueba en cada tick (independiente del
    // throttle de polling HTTP de mas abajo), igual que el NFC, para no
    // esperar hasta el siguiente poll programado para reaccionar a la hora
    // configurada de entrada/salida.
    handleAutoSign(nowMinutes, timeinfo);

    // NFC: escucha continuamente en cada tick, independiente del throttle de
    // polling HTTP de mas abajo, mientras el dispositivo esta encendido
    // (unico PollMode distinto de OFF, ver arriba).
    if (nfcSignState_ == NfcSignState::RESULT) {
        if (millis() < nfcSignResultUntilMs_) {
            return;  // se sigue mostrando el resultado, no lo pisa el color normal de abajo
        }
        nfcSignState_ = NfcSignState::IDLE;
    }

    String uid;
    if (nfcReader_.poll(uid)) {
        logPrintf("NFC: tarjeta detectada (UID %s).\n", uid.c_str());
        setLed(ledRotateFast());

        if (config_.hasLearnedCard() && uid == config_.learnedCardUid()) {
            const char* accion = (lastWoffuStatus_ == WoffuStatus::CLOCKED_IN) ? "salida" : "entrada";
            logPrintf("NFC: UID coincide con la tarjeta aprendida. Fichando %s...\n", accion);
            if (woffuClient_.toggleSign()) {
                logPrintf("Woffu API: fichaje de %s registrado por NFC.\n", accion);
                remote_.sendEvent("sign", String("Fichaje de ") + accion + " registrado por NFC.");
                setLed(ledFastBlink(LedColor::GREEN));
            } else {
                logPrintln("Woffu API: error al registrar el fichaje por NFC.");
                remote_.sendEvent("error", "Error al registrar el fichaje por NFC.");
                setLed(ledFastBlink(LedColor::YELLOW));
            }
        } else {
            logPrintln("NFC: UID no coincide con la tarjeta aprendida.");
            remote_.sendEvent("nfc", "Tarjeta NFC no reconocida.");
            setLed(ledFastBlink(LedColor::RED));
        }

        // Fuerza un repoll real justo despues de la ventana de resultado (en
        // vez de esperar al siguiente ciclo normal, que puede tardar hasta
        // ~60s): sin esto el LED se quedaba parpadeando indefinidamente tras
        // un mismatch en vez de volver a mostrar el estado real de Woffu.
        nextPollAtMs_ = millis();
        nfcSignState_ = NfcSignState::RESULT;
        nfcSignResultUntilMs_ = millis() + kNfcResultHoldMs;
        return;
    }

    if (millis() < nextPollAtMs_) {
        return;
    }
    nextPollAtMs_ = millis() + kPollIntervalMs;
    pollWoffuNow(mode);
}

void AppStateMachine::pollWoffuNow(PollMode mode) {
    WoffuStatus status = woffuClient_.fetchStatus();
    setWoffuStatus(status);
    lastPollEpoch_ = time(nullptr);
    logPrintf("Estado Woffu: %s\n", woffuStatusName(status));
    if (mode != PollMode::ACTIVE || nfcSignState_ != NfcSignState::IDLE) {
        return;
    }
    if (woffuClient_.credentialsInvalid()) {
        showStatusLed(ledSlowBlink(LedColor::YELLOW));
    } else {
        showStatusLed(ledForStatus(status));
    }
}

void AppStateMachine::setWoffuStatus(WoffuStatus status) {
    if (status != lastWoffuStatus_) {
        remote_.requestStatus();
    }
    lastWoffuStatus_ = status;
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
        logPrintf("Woffu: jornada de hoy - fin de semana=%s, festivo=%s.\n", info.isWeekend ? "si" : "no",
                  info.isHoliday ? "si" : "no");
    } else {
        workdayValid_ = false;
        logPrintln(
            "Woffu: no se pudo obtener la jornada de hoy; se usara polling activo por defecto hasta manana.");
    }
}

void AppStateMachine::handleAutoSign(uint16_t nowMinutes, const struct tm& timeinfo) {
    const DeviceConfig& cfg = config_.get();
    if (!cfg.autoSignEnabled) {
        return;
    }

    // tm_wday: 0=domingo..6=sabado. Solo aplica de lunes(1) a viernes(5); el
    // horario configurado no cubre fin de semana.
    int wday = timeinfo.tm_wday;
    if (wday < 1 || wday > 5) {
        return;
    }
    const TimeWindow& schedule = cfg.autoSignSchedule[wday - 1];

    // isWeekend es redundante con el check de tm_wday de arriba (mismo
    // calendario), pero isHoliday no lo es: un festivo entre semana no debe
    // disparar el fichaje automatico. Si todavia no hay jornada valida (fallo
    // de red, arranque reciente) se deja pasar de todas formas, mismo
    // criterio "agresivo pero seguro" que el resto del Scheduler.
    bool isHolidayToday = workdayValid_ && (workdayInfo_.isWeekend || workdayInfo_.isHoliday);

    if (nowMinutes >= schedule.startMinutes && lastAutoEntryYday_ != timeinfo.tm_yday) {
        lastAutoEntryYday_ = timeinfo.tm_yday;
        if (isHolidayToday) {
            logPrintln("Fichaje automatico: hoy es festivo/fin de semana segun Woffu, se omite la entrada.");
        } else {
            attemptAutoSign(true);
        }
    }

    if (nowMinutes >= schedule.endMinutes && lastAutoExitYday_ != timeinfo.tm_yday) {
        lastAutoExitYday_ = timeinfo.tm_yday;
        if (isHolidayToday) {
            logPrintln("Fichaje automatico: hoy es festivo/fin de semana segun Woffu, se omite la salida.");
        } else {
            attemptAutoSign(false);
        }
    }
}

void AppStateMachine::attemptAutoSign(bool isEntry) {
    const char* label = isEntry ? "entrada" : "salida";
    logPrintf("Fichaje automatico: hora de %s alcanzada, comprobando estado actual en Woffu...\n", label);
    String message;
    SignOutcome outcome = signTowards(isEntry, "automatico", message);
    remote_.sendEvent(outcome == SignOutcome::SIGNED    ? "sign"
                      : outcome == SignOutcome::ALREADY ? "info"
                                                        : "error",
                      "Fichaje automatico: " + message);
    // Fuerza un repoll real inmediato para que el LED refleje el resultado
    // (exito o fallo) sin esperar al siguiente ciclo de polling normal.
    nextPollAtMs_ = millis();
}

SignOutcome AppStateMachine::signTowards(bool isEntry, const char* origin, String& message) {
    const char* label = isEntry ? "entrada" : "salida";

    // Se pide el estado real (en vez de fiarse de lastWoffuStatus_, que puede
    // llevar hasta un ciclo de polling desactualizado) justo antes de decidir:
    // como fichar/desfichar es la misma llamada, fiarse de un estado obsoleto
    // podria disparar la accion contraria a la deseada.
    WoffuStatus status = woffuClient_.fetchStatus();
    setWoffuStatus(status);
    lastPollEpoch_ = time(nullptr);
    logPrintf("Estado Woffu: %s\n", woffuStatusName(status));

    if (status == WoffuStatus::UNKNOWN) {
        message = "no se pudo confirmar el estado actual en Woffu, se omite por seguridad.";
        logPrintf("Fichaje %s: %s\n", origin, message.c_str());
        return SignOutcome::STATE_UNKNOWN;
    }

    bool shouldSign = isEntry ? (status == WoffuStatus::CLOCKED_OUT) : (status == WoffuStatus::CLOCKED_IN);
    if (!shouldSign) {
        message = String("ya estaba fichada la ") + label + ", no se hace nada.";
        logPrintf("Fichaje %s: %s\n", origin, message.c_str());
        return SignOutcome::ALREADY;
    }

    if (woffuClient_.toggleSign()) {
        message = String("fichaje de ") + label + " registrado.";
        logPrintf("Woffu API: fichaje %s de %s registrado.\n", origin, label);
        // Woffu ya ha cambiado de estado; se refleja sin esperar al proximo
        // poll (que lo confirmara con el estado real).
        setWoffuStatus(isEntry ? WoffuStatus::CLOCKED_IN : WoffuStatus::CLOCKED_OUT);
        return SignOutcome::SIGNED;
    }
    message = String("error al registrar el fichaje de ") + label + " en Woffu.";
    logPrintf("Woffu API: error al registrar el fichaje %s de %s.\n", origin, label);
    return SignOutcome::FAILED;
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
    setLed(portalClientConnected_ ? ledAllSolid() : ledSlowBlink(LedColor::ALL));
}

void AppStateMachine::setLed(const LedCommand& command) {
    if (ledSentValid_ && command.color == lastLedSent_.color && command.mode == lastLedSent_.mode) {
        return;
    }
    led_.set(command);
    lastLedSent_ = command;
    ledSentValid_ = true;
    remote_.requestStatus();
}

void AppStateMachine::showStatusLed(const LedCommand& command) {
    statusLed_ = command;
    statusLedValid_ = true;
    setLed(overrideActive_ ? override_ : command);
}

void AppStateMachine::reapplyStatusLed(PollMode mode) {
    // Solo si ya hay un color de reposo que mostrar (no mientras se esta
    // cargando, ni fuera de horario, ni durante un feedback NFC: en esos
    // casos showStatusLed() aplicara el override la proxima vez).
    if (mode == PollMode::ACTIVE && statusLedValid_ && nfcSignState_ == NfcSignState::IDLE) {
        setLed(overrideActive_ ? override_ : statusLed_);
    }
}

void AppStateMachine::handleRemote(PollMode mode) {
    if (!remote_.enabled()) {
        return;
    }
    remote_.loop();

    RemoteCommand command;
    while (remote_.takeCommand(command)) {
        executeRemoteCommand(command, mode);
    }

    if (remote_.takeStatusDue()) {
        publishStatus(mode);
    }
}

void AppStateMachine::executeRemoteCommand(const RemoteCommand& command, PollMode mode) {
    logPrintf("Control remoto: comando '%s' recibido.\n", command.cmd.c_str());

    if (command.cmd == "status") {
        remote_.requestStatus();
        remote_.sendAck(command.id, true, "ok");
    } else if (command.cmd == "refresh") {
        pollWoffuNow(mode);
        remote_.requestStatus();
        remote_.sendAck(command.id, lastWoffuStatus_ != WoffuStatus::UNKNOWN,
                        String("Estado Woffu: ") + woffuStatusName(lastWoffuStatus_));
    } else if (command.cmd == "sign_in" || command.cmd == "sign_out") {
        bool isEntry = command.cmd == "sign_in";
        String message;
        SignOutcome outcome = signTowards(isEntry, "remoto", message);
        bool ok = outcome == SignOutcome::SIGNED || outcome == SignOutcome::ALREADY;
        remote_.sendAck(command.id, ok, message);
        if (outcome == SignOutcome::SIGNED) {
            remote_.sendEvent("sign", "Fichaje remoto: " + message);
        }
        // Repoll inmediato para confirmar el estado real (y el LED).
        nextPollAtMs_ = millis();
        if (mode != PollMode::ACTIVE) {
            pollWoffuNow(mode);
        }
    } else if (command.cmd == "led_set") {
        LedCommand wanted;
        if (!parseOverride(command.color, command.mode, wanted)) {
            remote_.sendAck(command.id, false, "color/modo no validos");
            return;
        }
        overrideActive_ = true;
        override_ = wanted;
        config_.setLedOverride(static_cast<uint8_t>(wanted.color), static_cast<uint8_t>(wanted.mode));
        logPrintf("Semaforo: color fijo %s (%s) desde el control remoto.\n", ledColorName(wanted.color),
                  ledModeName(wanted.mode));
        reapplyStatusLed(mode);
        remote_.requestStatus();
        remote_.sendAck(command.id, true, "color fijo aplicado");
        remote_.sendEvent("led", String("Color fijo: ") + ledColorName(wanted.color) + " (" +
                                     ledModeName(wanted.mode) + ")");
    } else if (command.cmd == "led_auto") {
        overrideActive_ = false;
        config_.clearLedOverride();
        logPrintln("Semaforo: vuelta al color automatico desde el control remoto.");
        reapplyStatusLed(mode);
        remote_.requestStatus();
        remote_.sendAck(command.id, true, "modo automatico");
        remote_.sendEvent("led", "Color automatico (estado de Woffu)");
    } else if (command.cmd == "reboot") {
        remote_.sendAck(command.id, true, "reiniciando");
        logPrintln("Control remoto: reinicio solicitado.");
        // Unas vueltas de loop() para que el ack y la ultima linea de log
        // salgan antes de cortar la conexion.
        for (int i = 0; i < 5; i++) {
            remote_.loop();
            delay(100);
        }
        ESP.restart();
    } else if (command.cmd == "ota_update") {
        String latest;
        switch (otaUpdater_.checkForUpdate(latest)) {
            case OtaCheckResult::UP_TO_DATE:
                remote_.sendAck(command.id, true, String("Ya esta en la ultima version (") + FIRMWARE_VERSION + ").");
                return;
            case OtaCheckResult::ERROR:
                remote_.sendAck(command.id, false, "Error comprobando la actualizacion: " + otaUpdater_.lastErrorDetail());
                return;
            case OtaCheckResult::AVAILABLE:
                break;
        }
        remote_.sendAck(command.id, true, String("Actualizando de ") + FIRMWARE_VERSION + " a " + latest +
                                              "; el dispositivo se reiniciara solo.");
        logPrintf("OTA remota: actualizando %s -> %s...\n", FIRMWARE_VERSION, latest.c_str());
        remote_.loop();
        lastLoggedOtaPercent_ = -1;
        setLed(ledRotateFast());
        OtaResult result = otaUpdater_.update(latest);
        if (result == OtaResult::ERROR) {
            config_.clearOtaNote();
            logPrintf("OTA remota: error (%s).\n", otaUpdater_.lastErrorDetail().c_str());
            remote_.sendEvent("error", "OTA fallida: " + otaUpdater_.lastErrorDetail());
            nextPollAtMs_ = millis();
            if (mode == PollMode::ACTIVE) {
                reapplyStatusLed(mode);
            } else {
                setLed(ledOff());
            }
        }
    } else {
        remote_.sendAck(command.id, false, "comando desconocido: " + command.cmd);
    }
}

void AppStateMachine::publishStatus(PollMode mode) {
    JsonDocument doc;
    doc["fw"] = FIRMWARE_VERSION;
    doc["uptime_s"] = millis() / 1000;
    doc["epoch"] = static_cast<long>(time(nullptr));

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = WiFi.SSID();
    wifi["rssi"] = WiFi.RSSI();
    wifi["ip"] = wifi_.ipAddress();

    JsonObject heap = doc["heap"].to<JsonObject>();
    heap["free"] = ESP.getFreeHeap();
    heap["min_free"] = ESP.getMinFreeHeap();
    heap["max_alloc"] = ESP.getMaxAllocHeap();

    JsonObject woffu = doc["woffu"].to<JsonObject>();
    woffu["status"] = woffuStatusCode(lastWoffuStatus_);
    woffu["credentials_invalid"] = woffuClient_.credentialsInvalid();
    woffu["last_poll_epoch"] = static_cast<long>(lastPollEpoch_);

    doc["mode"] = mode == PollMode::ACTIVE ? "active" : "off";
    doc["mode_reason"] = pollModeReason_;

    JsonObject workday = doc["workday"].to<JsonObject>();
    workday["valid"] = workdayValid_;
    workday["weekend"] = workdayInfo_.isWeekend;
    workday["holiday"] = workdayInfo_.isHoliday;

    const DeviceConfig& cfg = config_.get();
    JsonObject window = doc["window"].to<JsonObject>();
    window["start"] = minutesToHhMm(cfg.activeWindow.startMinutes);
    window["end"] = minutesToHhMm(cfg.activeWindow.endMinutes);

    if (ledSentValid_) {
        ledToJson(doc["led"].to<JsonObject>(), lastLedSent_);
    }
    if (statusLedValid_) {
        ledToJson(doc["led_auto"].to<JsonObject>(), statusLed_);
    }
    if (overrideActive_) {
        ledToJson(doc["override"].to<JsonObject>(), override_);
    } else {
        doc["override"] = nullptr;
    }

    JsonObject nfc = doc["nfc"].to<JsonObject>();
    nfc["reader"] = nfcReaderOk_;
    nfc["card"] = config_.hasLearnedCard();

    JsonObject autoSign = doc["auto_sign"].to<JsonObject>();
    autoSign["enabled"] = cfg.autoSignEnabled;
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_wday >= 1 && timeinfo.tm_wday <= 5) {
        const TimeWindow& today = cfg.autoSignSchedule[timeinfo.tm_wday - 1];
        autoSign["today_start"] = minutesToHhMm(today.startMinutes);
        autoSign["today_end"] = minutesToHhMm(today.endMinutes);
    }

    remote_.sendStatus(doc);
}
