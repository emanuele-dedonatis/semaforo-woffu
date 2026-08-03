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
    // Ficha/desficha (toggle): el propio backend de Woffu decide la direccion
    // segun el ultimo estado registrado del usuario, no hay parametro para
    // elegirla (ver tools/toggle_sign.py, que verifica este comportamiento
    // contra la API real).
    bool toggleSign();
    bool credentialsInvalid() const { return credentialsInvalid_; }

private:
    bool login();
    bool fetchUserId();
    bool authenticatedGet(const String& url, JsonDocument& doc);
    bool authenticatedPostJson(const String& url, const String& jsonBody);

    String username_;
    String password_;
    String accessToken_;
    String userId_;
    bool credentialsInvalid_ = false;
};
