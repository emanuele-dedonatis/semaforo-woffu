#pragma once

#include <Arduino.h>

enum class OtaResult : uint8_t { UP_TO_DATE, UPDATED, ERROR };

class OtaUpdater {
public:
    OtaResult checkAndUpdate();
    const String& lastErrorDetail() const { return lastErrorDetail_; }

private:
    String lastErrorDetail_;
};
