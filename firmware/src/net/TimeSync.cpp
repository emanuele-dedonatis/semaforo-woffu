#include "TimeSync.h"

#include <time.h>

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "Log.h"

namespace {
// Geolocalizacion por IP publica (ip-api.com, HTTP plano - sin datos sensibles de por
// medio, no hace falta el certificate bundle de las llamadas a Woffu/GitHub). Solo se
// pide el offset UTC actual (ya incluye el ajuste de horario de verano si aplica), no
// hace falta resolver reglas de DST a mano.
constexpr const char* kGeoIpUrl = "http://ip-api.com/json/?fields=status,message,offset,timezone,city,country";

// Sin reintentos ante fallo (mismo criterio que el resto del firmware, ver Requisitos.md):
// si falla, se deja el offset a 0 (UTC) y se reintenta en la siguiente reconexion WiFi.
bool fetchUtcOffsetSeconds(long& outOffsetSeconds) {
    HTTPClient http;
    if (!http.begin(kGeoIpUrl)) {
        logPrintf("Geolocalizacion IP: no se pudo iniciar la conexion a %s\n", kGeoIpUrl);
        return false;
    }
    http.useHTTP10(true);

    int status = http.GET();
    logPrintf("Geolocalizacion IP: GET %s -> %d\n", kGeoIpUrl, status);
    if (status != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();
    if (error) {
        logPrintf("Geolocalizacion IP: error al parsear la respuesta (%s)\n", error.c_str());
        return false;
    }

    String status_str = doc["status"] | "";
    if (status_str != "success") {
        logPrintf("Geolocalizacion IP: respuesta no valida (%s)\n", (doc["message"] | "sin detalle"));
        return false;
    }

    outOffsetSeconds = doc["offset"] | 0;
    const char* city = doc["city"] | "?";
    const char* country = doc["country"] | "?";
    const char* timezoneName = doc["timezone"] | "?";
    logPrintf("Geolocalizacion IP: %s, %s (%s), UTC%+ld min\n", city, country, timezoneName,
              outOffsetSeconds / 60);
    return true;
}
}  // namespace

void TimeSync::begin() {
    long offsetSeconds = 0;
    if (!fetchUtcOffsetSeconds(offsetSeconds)) {
        logPrintln("Geolocalizacion IP: no se pudo determinar la zona horaria, usando UTC por defecto.");
    }
    configTime(offsetSeconds, 0, "pool.ntp.org", "time.nist.gov");
}

bool TimeSync::isSynced() const {
    // No usar getLocalTime(&info, 0): su bucle interno basado en millis() con
    // timeout 0 puede salir sin comprobar la hora si se cruza un tick de millis()
    // justo entre la captura del "start" y la condición del while, devolviendo
    // false esporadicamente aunque la hora ya este sincronizada. Comprobar
    // directamente el epoch evita esa carrera.
    return time(nullptr) > kNtpSyncedThreshold;
}
