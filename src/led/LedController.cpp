#include "LedController.h"

void LedController::begin(uint8_t pinRed, uint8_t pinYellow, uint8_t pinGreen) {
    pinRed_ = pinRed;
    pinYellow_ = pinYellow;
    pinGreen_ = pinGreen;

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

    for (;;) {
        xQueueReceive(self->commandQueue_, &current, portMAX_DELAY);
    }
}
