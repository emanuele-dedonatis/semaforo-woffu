#include "LedController.h"

namespace {
constexpr uint32_t kPwmFreqHz = 5000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr TickType_t kSlowPeriod = pdMS_TO_TICKS(1000);
constexpr TickType_t kFastPeriod = pdMS_TO_TICKS(250);
constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(50);

// El verde de este módulo se ve más apagado que rojo/amarillo al mismo duty
// (Vf del LED verde más alta, misma resistencia en serie que los demás en
// el módulo -> menos corriente). Se compensa con una ganancia por canal;
// ajustar kGreenScalePercent a ojo si tras un cambio de módulo vuelve a
// verse desequilibrado.
constexpr uint16_t kRedScalePercent = 100;
constexpr uint16_t kYellowScalePercent = 100;
constexpr uint16_t kGreenScalePercent = 180;

uint32_t scaledDuty(uint32_t duty, uint16_t scalePercent) {
    uint32_t scaled = (duty * scalePercent) / 100;
    return scaled > 255 ? 255 : scaled;
}
}

void LedController::begin(uint8_t pinRed, uint8_t pinYellow, uint8_t pinGreen) {
    ledcSetup(kChannelRed, kPwmFreqHz, kPwmResolutionBits);
    ledcSetup(kChannelYellow, kPwmFreqHz, kPwmResolutionBits);
    ledcSetup(kChannelGreen, kPwmFreqHz, kPwmResolutionBits);
    ledcAttachPin(pinRed, kChannelRed);
    ledcAttachPin(pinYellow, kChannelYellow);
    ledcAttachPin(pinGreen, kChannelGreen);

    commandQueue_ = xQueueCreate(1, sizeof(LedCommand));
    xTaskCreate(taskFn, "led", 2048, this, 1, &taskHandle_);
}

void LedController::set(const LedCommand& command) {
    if (commandQueue_ != nullptr) {
        xQueueOverwrite(commandQueue_, &command);
    }
}

void LedController::taskFn(void* params) {
    auto* self = static_cast<LedController*>(params);
    LedCommand current;
    bool phaseOn = true;
    TickType_t lastToggle = xTaskGetTickCount();

    for (;;) {
        if (xQueueReceive(self->commandQueue_, &current, kPollPeriod) == pdTRUE) {
            phaseOn = true;
            lastToggle = xTaskGetTickCount();
        }

        TickType_t blinkPeriod = 0;
        if (current.mode == LedMode::BLINK_SLOW) {
            blinkPeriod = kSlowPeriod;
        } else if (current.mode == LedMode::BLINK_FAST) {
            blinkPeriod = kFastPeriod;
        }

        if (blinkPeriod > 0 && (xTaskGetTickCount() - lastToggle) >= blinkPeriod) {
            phaseOn = !phaseOn;
            lastToggle = xTaskGetTickCount();
        }

        bool lit = (current.mode == LedMode::SOLID) || (blinkPeriod > 0 && phaseOn);
        uint32_t duty = lit ? current.brightness : 0;

        bool redOn = current.color == LedColor::RED || current.color == LedColor::ALL;
        bool yellowOn = current.color == LedColor::YELLOW || current.color == LedColor::ALL;
        bool greenOn = current.color == LedColor::GREEN || current.color == LedColor::ALL;

        ledcWrite(kChannelRed, redOn ? scaledDuty(duty, kRedScalePercent) : 0);
        ledcWrite(kChannelYellow, yellowOn ? scaledDuty(duty, kYellowScalePercent) : 0);
        ledcWrite(kChannelGreen, greenOn ? scaledDuty(duty, kGreenScalePercent) : 0);
    }
}
