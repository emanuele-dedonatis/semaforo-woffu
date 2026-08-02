#pragma once

#include <Arduino.h>
#include <time.h>

// Cualquier instante posterior a esta fecha (2021-01-01 UTC) sirve para distinguir
// una hora ya sincronizada por NTP de la epoca por defecto (1970) tras arrancar.
// Tambien la usa Log.cpp para decidir si puede timestampar con hora real o con uptime.
constexpr time_t kNtpSyncedThreshold = 1609459200;

class TimeSync {
public:
    // Detecta la zona horaria por geolocalizacion de la IP publica (ver TimeSync.cpp)
    // y sincroniza la hora por NTP con ese offset. Sin parametros: ya no hace falta
    // que el usuario configure manualmente el TZ POSIX.
    void begin();
    bool isSynced() const;
};
