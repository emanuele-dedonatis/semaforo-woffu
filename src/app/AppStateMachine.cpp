#include "AppStateMachine.h"

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;

LedCommand ledForUnconfigured(uint8_t brightness) {
    LedCommand cmd;
    cmd.color = LedColor::ALL;
    cmd.mode = LedMode::BLINK_SLOW;
    cmd.brightness = brightness;
    return cmd;
}

LedCommand ledForBleWindow(WoffuStatus status, uint8_t brightness) {
    LedCommand cmd;
    cmd.color = LedColor::YELLOW;
    if (status == WoffuStatus::CLOCKED_IN) {
        cmd.color = LedColor::GREEN;
    } else if (status == WoffuStatus::CLOCKED_OUT) {
        cmd.color = LedColor::RED;
    }
    cmd.mode = LedMode::BLINK_SLOW;
    cmd.brightness = brightness;
    return cmd;
}
}

void AppStateMachine::begin() {
    config_.begin();
    led_.begin(kPinLedRed, kPinLedYellow, kPinLedGreen);

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
    led_.set(ledForUnconfigured(config_.get().brightness));
}

void AppStateMachine::enterBleWindow() {
    ble_.begin();
    led_.set(ledForBleWindow(WoffuStatus::UNKNOWN, config_.get().brightness));
}

void AppStateMachine::enterRunning() {
    ble_.stop();
}

void AppStateMachine::handleBleEvents() {
    BleEvent event;
    while (ble_.pollEvent(event)) {
    }
}
