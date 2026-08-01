#include "TimeSync.h"
#include <time.h>

namespace {
// Cualquier instante posterior a esta fecha (2021-01-01 UTC) sirve para distinguir
// una hora ya sincronizada por NTP de la epoca por defecto (1970) tras arrancar.
constexpr time_t kSyncedThreshold = 1609459200;
}

void TimeSync::begin(const String& posixTimezone) {
    configTzTime(posixTimezone.c_str(), "pool.ntp.org", "time.nist.gov");
}

bool TimeSync::isSynced() const {
    // No usar getLocalTime(&info, 0): su bucle interno basado en millis() con
    // timeout 0 puede salir sin comprobar la hora si se cruza un tick de millis()
    // justo entre la captura del "start" y la condición del while, devolviendo
    // false esporadicamente aunque la hora ya este sincronizada. Comprobar
    // directamente el epoch evita esa carrera.
    return time(nullptr) > kSyncedThreshold;
}
