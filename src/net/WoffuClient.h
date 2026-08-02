#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class WoffuStatus : uint8_t { UNKNOWN, CLOCKED_IN, CLOCKED_OUT };

struct WorkdayInfo {
    uint16_t startMinutes = 0;
    uint16_t endMinutes = 0;
    bool isWeekend = false;
    bool isHoliday = false;
};

class WoffuClient {
public:
    void begin(const String& username, const String& password);
    WoffuStatus fetchStatus();
    bool fetchWorkday(WorkdayInfo& out);

private:
    bool login();
    bool fetchUserId();
    bool authenticatedGet(const String& url, JsonDocument& doc);

    String username_;
    String password_;
    String accessToken_;
    String userId_;
};
