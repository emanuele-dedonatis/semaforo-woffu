#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class WoffuStatus : uint8_t { UNKNOWN, CLOCKED_IN, CLOCKED_OUT };

struct WorkdayInfo {
    bool isWeekend = false;
    bool isHoliday = false;
};

// Lo fichado hoy, sacado de los slots de cada fetchStatus().
struct TodayWork {
    bool valid = false;           // hay datos de un fetchStatus() correcto
    bool anySlot = false;         // hoy hay al menos un fichaje
    uint32_t closedSeconds = 0;   // suma de los tramos ya cerrados (entrada y salida)
    int32_t openSinceSec = -1;    // inicio del tramo abierto (segundos desde medianoche), -1 si no hay
};

// Totales de diaries/summary/presence para un rango de dias.
struct DiariesTotals {
    long differenceSeconds = 0;   // trabajado - teorico (totalDifferenceTime)
    long scheduleSeconds = 0;     // teorico (totalScheduleTimeFormatted)
};

class WoffuClient {
public:
    void begin(const String& username, const String& password);
    WoffuStatus fetchStatus();
    bool fetchWorkday(WorkdayInfo& out);
    const TodayWork& todayWork() const { return today_; }
    // Totales de los diarios entre dos fechas (YYYY-MM-DD, ambas incluidas).
    bool fetchDiariesTotals(const String& fromDate, const String& toDate, int days, DiariesTotals& out);
    // Ficha/desficha (toggle): el propio backend de Woffu decide la direccion
    // segun el ultimo estado registrado del usuario, no hay parametro para
    // elegirla (ver tools/toggle_sign.py, que verifica este comportamiento
    // contra la API real).
    bool toggleSign();
    bool credentialsInvalid() const { return credentialsInvalid_; }

private:
    bool login();
    bool fetchUserId();
    bool authenticatedGet(const String& url, JsonDocument& doc, const JsonDocument* filter = nullptr,
                          int* statusOut = nullptr);
    bool discoverDiariesHost();
    void updateTodayWork(JsonArrayConst slots);
    bool authenticatedPostJson(const String& url, const String& jsonBody);

    String username_;
    String password_;
    String accessToken_;
    String userId_;
    String diariesHost_ = "app.woffu.com";
    bool diariesHostDiscovered_ = false;
    TodayWork today_;
    bool credentialsInvalid_ = false;
};
