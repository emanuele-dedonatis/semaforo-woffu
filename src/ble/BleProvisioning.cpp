#include "BleProvisioning.h"

void BleProvisioning::begin() {
    eventQueue_ = xQueueCreate(8, sizeof(BleEvent));
}

void BleProvisioning::stop() {
}

bool BleProvisioning::pollEvent(BleEvent& outEvent) {
    if (eventQueue_ == nullptr) {
        return false;
    }
    return xQueueReceive(eventQueue_, &outEvent, 0) == pdTRUE;
}

void BleProvisioning::notifyStatus(const String& statusJson) {
}
