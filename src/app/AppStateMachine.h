#pragma once

#include <Arduino.h>
#include "config/Config.h"
#include "app/Scheduler.h"
#include "ble/BleProvisioning.h"
#include "led/LedController.h"
#include "net/WifiManager.h"
#include "net/TimeSync.h"
#include "net/WoffuClient.h"
#include "net/OtaUpdater.h"

enum class AppState : uint8_t {
    INIT,
    UNCONFIGURED,
    BLE_WINDOW,
    RUNNING,
};

class AppStateMachine {
public:
    void begin();
    void loop();

private:
    void enterUnconfigured();
    void enterBleWindow();
    void enterRunning();
    void handleBleEvents();

    AppState state_ = AppState::INIT;
    Config config_;
    Scheduler scheduler_;
    BleProvisioning ble_;
    LedController led_;
    WifiManager wifi_;
    TimeSync timeSync_;
    WoffuClient woffuClient_;
    OtaUpdater otaUpdater_;
};
