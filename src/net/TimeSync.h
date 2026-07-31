#pragma once

#include <Arduino.h>

class TimeSync {
public:
    void begin(const String& posixTimezone);
    bool isSynced() const;
};
