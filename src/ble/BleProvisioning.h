#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config/Config.h"

enum class BleEventType : uint8_t {
    CLIENT_CONNECTED,
    CLIENT_DISCONNECTED,
    CONFIG_RECEIVED,
    OTA_REQUESTED,
    FACTORY_RESET_REQUESTED,
};

struct BleEvent {
    BleEventType type;
};

class BleProvisioning {
public:
    void begin();
    void stop();
    bool pollEvent(BleEvent& outEvent);
    void notifyStatus(const String& statusJson);

private:
    QueueHandle_t eventQueue_ = nullptr;
};
