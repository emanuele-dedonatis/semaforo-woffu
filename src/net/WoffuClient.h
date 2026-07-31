#pragma once

#include <Arduino.h>

enum class WoffuStatus : uint8_t { UNKNOWN, CLOCKED_IN, CLOCKED_OUT };

class WoffuClient {
public:
    void begin(const String& username, const String& password);
    WoffuStatus fetchStatus();

private:
    bool login();

    String username_;
    String password_;
    String accessToken_;
};
