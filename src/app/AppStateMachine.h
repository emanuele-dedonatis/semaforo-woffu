#pragma once

#include <Arduino.h>
#include "config/Config.h"
#include "app/Scheduler.h"
#include "web/ProvisioningPortal.h"
#include "led/LedController.h"
#include "net/WifiManager.h"
#include "net/TimeSync.h"
#include "net/WoffuClient.h"
#include "net/OtaUpdater.h"

enum class AppState : uint8_t {
    INIT,
    UNCONFIGURED,
    PORTAL_WINDOW,
    RUNNING,
};

class AppStateMachine {
public:
    void begin();
    void loop();

private:
    void enterUnconfigured();
    void enterPortalWindow();
    void enterRunning();
    void handlePortal();
    void handleRunning();
    void saveConfigAndReboot(const DeviceConfig& newConfig);
    void updateLedForCurrentState();

    AppState state_ = AppState::INIT;
    uint32_t portalWindowDeadlineMs_ = 0;
    bool portalClientConnected_ = false;
    PollMode runningPollMode_ = PollMode::OFF;
    bool runningPollModeKnown_ = false;
    uint32_t nextPollAtMs_ = 0;
    bool wifiWasConnected_ = false;
    uint32_t wifiConnectedAtMs_ = 0;
    bool timeWasSynced_ = false;
    bool timeSyncTimeoutLogged_ = false;
    Config config_;
    Scheduler scheduler_;
    ProvisioningPortal portal_;
    LedController led_;
    WifiManager wifi_;
    TimeSync timeSync_;
    WoffuClient woffuClient_;
    OtaUpdater otaUpdater_;
};
