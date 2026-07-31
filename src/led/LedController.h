#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class LedColor : uint8_t { OFF, RED, YELLOW, GREEN };
enum class LedMode : uint8_t { OFF, SOLID, BLINK_SLOW, BLINK_FAST };

struct LedCommand {
    LedColor color = LedColor::OFF;
    LedMode mode = LedMode::OFF;
    uint8_t brightness = 255;
};

class LedController {
public:
    void begin(uint8_t pinRed, uint8_t pinYellow, uint8_t pinGreen);
    void set(const LedCommand& command);

private:
    static void taskFn(void* params);

    uint8_t pinRed_ = 0;
    uint8_t pinYellow_ = 0;
    uint8_t pinGreen_ = 0;
    QueueHandle_t commandQueue_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
};
