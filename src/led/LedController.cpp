#include "LedController.h"

namespace {
constexpr TickType_t kSlowPeriod = pdMS_TO_TICKS(1000);
constexpr TickType_t kFastPeriod = pdMS_TO_TICKS(250);
constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(50);
constexpr TickType_t kRotatePeriod = pdMS_TO_TICKS(1000);
constexpr LedColor kRotateSequence[] = {LedColor::GREEN, LedColor::YELLOW, LedColor::RED};
constexpr size_t kRotateSteps = sizeof(kRotateSequence) / sizeof(kRotateSequence[0]);
}

void LedController::begin(uint8_t pinRed, uint8_t pinYellow, uint8_t pinGreen) {
    pinRed_ = pinRed;
    pinYellow_ = pinYellow;
    pinGreen_ = pinGreen;

    pinMode(pinRed_, OUTPUT);
    pinMode(pinYellow_, OUTPUT);
    pinMode(pinGreen_, OUTPUT);

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
    size_t rotateIndex = 0;
    TickType_t lastRotate = xTaskGetTickCount();

    for (;;) {
        if (xQueueReceive(self->commandQueue_, &current, kPollPeriod) == pdTRUE) {
            phaseOn = true;
            lastToggle = xTaskGetTickCount();
            rotateIndex = 0;
            lastRotate = xTaskGetTickCount();
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

        LedColor activeColor = current.color;
        bool lit;
        if (current.mode == LedMode::ROTATE) {
            if ((xTaskGetTickCount() - lastRotate) >= kRotatePeriod) {
                rotateIndex = (rotateIndex + 1) % kRotateSteps;
                lastRotate = xTaskGetTickCount();
            }
            activeColor = kRotateSequence[rotateIndex];
            lit = true;
        } else {
            lit = (current.mode == LedMode::SOLID) || (blinkPeriod > 0 && phaseOn);
        }

        bool redOn = lit && (activeColor == LedColor::RED || activeColor == LedColor::ALL);
        bool yellowOn = lit && (activeColor == LedColor::YELLOW || activeColor == LedColor::ALL);
        bool greenOn = lit && (activeColor == LedColor::GREEN || activeColor == LedColor::ALL);

        digitalWrite(self->pinRed_, redOn ? HIGH : LOW);
        digitalWrite(self->pinYellow_, yellowOn ? HIGH : LOW);
        digitalWrite(self->pinGreen_, greenOn ? HIGH : LOW);
    }
}
