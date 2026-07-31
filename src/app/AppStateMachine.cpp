#include "AppStateMachine.h"

void AppStateMachine::begin() {
    config_.begin();
    state_ = config_.get().configured ? AppState::BLE_WINDOW : AppState::UNCONFIGURED;

    switch (state_) {
        case AppState::UNCONFIGURED:
            enterUnconfigured();
            break;
        case AppState::BLE_WINDOW:
            enterBleWindow();
            break;
        default:
            break;
    }
}

void AppStateMachine::loop() {
    handleBleEvents();
}

void AppStateMachine::enterUnconfigured() {
    ble_.begin();
}

void AppStateMachine::enterBleWindow() {
    ble_.begin();
}

void AppStateMachine::enterRunning() {
    ble_.stop();
}

void AppStateMachine::handleBleEvents() {
    BleEvent event;
    while (ble_.pollEvent(event)) {
    }
}
