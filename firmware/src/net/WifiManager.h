#pragma once

#include <Arduino.h>

class WifiManager {
public:
    void begin(const String& ssid, const String& password);
    bool isConnected() const;
    String ipAddress() const;
};
