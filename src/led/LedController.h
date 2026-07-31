#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class LedColor : uint8_t { OFF, RED, YELLOW, GREEN, ALL };
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

    static constexpr uint8_t kChannelRed = 0;
    static constexpr uint8_t kChannelYellow = 1;
    static constexpr uint8_t kChannelGreen = 2;

    QueueHandle_t commandQueue_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
};
