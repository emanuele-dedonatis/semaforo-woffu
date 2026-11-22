#pragma once

#include <Arduino.h>
#include <time.h>
#include "config/Config.h"
#include "app/Scheduler.h"
#include "web/ProvisioningPortal.h"
#include "led/LedController.h"
#include "net/WifiManager.h"
#include "net/TimeSync.h"
#include "net/WoffuClient.h"
#include "net/OtaUpdater.h"
#include "nfc/NfcReader.h"

enum class AppState : uint8_t {
    INIT,
    CONNECTING,
    PORTAL_WINDOW,
    RUNNING,
};

class AppStateMachine {
public:
    void begin();
    void loop();

private:
    void enterConnecting();
    void enterPortalWindow();
    void enterRunning();
    void handleConnecting();
    void handlePortal();
    void handleRunning();
    void performOtaCheck();
    void saveConfigAndReboot(const DeviceConfig& newConfig);
    void updateLedForCurrentState();
    void refreshWorkdayInfo(int yday);
    void handleAutoSign(uint16_t nowMinutes, const struct tm& timeinfo);
    void attemptAutoSign(bool isEntry);

    AppState state_ = AppState::INIT;
    uint32_t connectingDeadlineMs_ = 0;
    uint32_t portalWindowDeadlineMs_ = 0;
    bool portalClientConnected_ = false;
    PollMode runningPollMode_ = PollMode::OFF;
    bool runningPollModeKnown_ = false;
    uint32_t nextPollAtMs_ = 0;
    WorkdayInfo workdayInfo_;
    bool workdayValid_ = false;
    WoffuStatus lastWoffuStatus_ = WoffuStatus::UNKNOWN;
    int lastWorkdayYday_ = -1;
    bool wifiWasConnected_ = false;
    uint32_t wifiConnectedAtMs_ = 0;
    uint32_t wifiDisconnectedSinceMs_ = 0;
    bool wifiConnectTimeoutLogged_ = false;
    bool timeWasSynced_ = false;
    bool timeSyncTimeoutLogged_ = false;
    bool otaCheckTriggered_ = false;
    int lastLoggedOtaPercent_ = -1;
    Config config_;
    Scheduler scheduler_;
    ProvisioningPortal portal_;
    LedController led_;
    WifiManager wifi_;
    TimeSync timeSync_;
    WoffuClient woffuClient_;
    OtaUpdater otaUpdater_;
    NfcReader nfcReader_;

    // Aprendizaje de tarjeta NFC (solo dentro de PORTAL_WINDOW).
    bool nfcLearnActive_ = false;
    uint32_t nfcLearnDeadlineMs_ = 0;
    uint32_t nfcLearnResultUntilMs_ = 0;

    // Fichaje por NFC (solo dentro de RUNNING + PollMode::ACTIVE).
    enum class NfcSignState : uint8_t { IDLE, RESULT };
    NfcSignState nfcSignState_ = NfcSignState::IDLE;
    uint32_t nfcSignResultUntilMs_ = 0;

    // Fichaje automatico (solo dentro de RUNNING, cualquier PollMode != OFF).
    // Se marca el yday en cuanto se intenta la entrada/salida del dia (haya
    // ido bien o mal), para no reintentar dentro del mismo dia (ver
    // handleAutoSign()).
    int lastAutoEntryYday_ = -1;
    int lastAutoExitYday_ = -1;
};
