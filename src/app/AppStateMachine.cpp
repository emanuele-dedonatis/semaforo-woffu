#include "AppStateMachine.h"

namespace {
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint32_t kPortalWaitMs = 30000; // espera inicial sin nadie conectado; una vez conectado no hay límite hasta que se desconecta

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

LedCommand ledAllSolid(uint8_t brightness) {
    LedCommand cmd;
    cmd.color = LedColor::ALL;
    cmd.mode = LedMode::SOLID;
    cmd.brightness = brightness;
    return cmd;
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

    static bool wasWifiConnected = false;
    bool wifiConnected = wifi_.isConnected();
    if (wifiConnected && !wasWifiConnected) {
        Serial.printf("WiFi conectado, IP: %s\n", wifi_.ipAddress().c_str());
        timeSync_.begin(config_.get().timezone);
    }
    wasWifiConnected = wifiConnected;

    if (state_ == AppState::PORTAL_WINDOW && !portalClientConnected_ && millis() >= portalWindowDeadlineMs_) {
        enterRunning();
    }
}

void AppStateMachine::enterUnconfigured() {
    portal_.begin(config_.get());
    updateLedForCurrentState();
}

void AppStateMachine::enterPortalWindow() {
    // El portal va solo (sin STA en paralelo): la conexión a la WiFi real
    // se pospone a enterRunning(), tras cerrarse la ventana de configuración.
    portal_.begin(config_.get());
    portalWindowDeadlineMs_ = millis() + kPortalWaitMs;
    updateLedForCurrentState();
}

void AppStateMachine::enterRunning() {
    state_ = AppState::RUNNING;
    portal_.stop();
    wifi_.begin(config_.get().wifiSsid, config_.get().wifiPassword);
    led_.set(ledOff());
}

void AppStateMachine::handlePortal() {
    portal_.loop();

    bool connected = portal_.hasClient();
    if (connected != portalClientConnected_) {
        portalClientConnected_ = connected;
        updateLedForCurrentState();
        if (!connected && state_ == AppState::PORTAL_WINDOW) {
            // Se acaba de desconectar el último cliente: cerramos ya la ventana.
            enterRunning();
            return;
        }
    }

    DeviceConfig newConfig;
    if (portal_.takeConfigToSave(newConfig)) {
        saveConfigAndReboot(newConfig);
        return;
    }

    if (portal_.takeOtaRequested()) {
        otaUpdater_.checkAndUpdate();
    }

    if (portal_.takeFactoryResetRequested()) {
        config_.factoryReset();
        delay(300);
        ESP.restart();
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
    uint8_t brightness = config_.get().brightness;
    led_.set(portalClientConnected_ ? ledAllSolid(brightness) : ledSlowBlink(LedColor::ALL, brightness));
}
