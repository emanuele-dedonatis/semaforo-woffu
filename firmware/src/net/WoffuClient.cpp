#include "WoffuClient.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Log.h"
#include "net/CertBundle.h"

namespace {
constexpr const char* kAuthUrl = "https://app.woffu.com/api/svc/accounts/authorization/token";
constexpr const char* kSlotsUrl = "https://app.woffu.com/api/svc/signs/v2/signs/slots";
constexpr const char* kUsersUrl = "https://app.woffu.com/api/users";
constexpr const char* kCompaniesUrl = "https://app.woffu.com/api/companies";
constexpr const char* kWorkdayUrlPrefix = "https://app.woffu.com/api/svc/core/users/";
constexpr const char* kWorkdayUrlSuffix = "/diarysummaries/workday";
// Verificado contra la API real con tools/toggle_sign.py: el body no lleva
// UserId (el servidor lo infiere del token), y fichar/desfichar es la misma
// llamada en ambos sentidos (el servidor decide segun el ultimo estado
// registrado del usuario).
constexpr const char* kSignsUrl = "https://app.woffu.com/api/svc/signs/signs";

// Minutos al oeste de UTC, misma convencion que JS Date.prototype.getTimezoneOffset()
// (positivo si la zona esta detras de UTC - signo invertido respecto a lo intuitivo,
// ver tools/toggle_sign.py). El toolchain de arduino-esp32 no expone tm_gmtoff en
// 'struct tm', asi que el offset se calcula por diferencia: se reinterpretan los
// campos de la hora UTC actual como si fueran hora local (mktime) y se compara con
// el instante real. Como TimeSync::begin() siempre llama a configTime() con un
// offset fijo (daylightOffset_sec=0, el ajuste de verano ya viene aplicado por
// ip-api.com), no hay reglas de DST que mktime() deba resolver aqui.
int timezoneOffsetMinutes() {
    time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    time_t utcFieldsAsLocal = mktime(&utc);
    long offsetSecondsEast = static_cast<long>(difftime(now, utcFieldsAsLocal));
    return static_cast<int>(-offsetSecondsEast / 60);
}

// Segundos desde medianoche de un lado (in/out) de un slot, o -1 si no hay.
// Mismo criterio que tools/balance.py: el lado puede ser un string ISO o un
// objeto con la hora en alguno de estos campos.
long slotSeconds(JsonVariantConst endpoint) {
    const char* text = nullptr;
    if (endpoint.is<const char*>()) {
        text = endpoint.as<const char*>();
    } else if (endpoint.is<JsonObjectConst>()) {
        for (const char* key : {"shortTrueTime", "shortTime", "trueTime", "time", "date"}) {
            if (endpoint[key].is<const char*>()) {
                text = endpoint[key].as<const char*>();
                break;
            }
        }
    }
    if (text == nullptr) {
        return -1;
    }
    const char* t = strchr(text, 'T');
    if (t != nullptr) {
        text = t + 1;
    }
    int h = 0, m = 0, sec = 0;
    int n = sscanf(text, "%d:%d:%d", &h, &m, &sec);
    if (n < 2) {
        return -1;
    }
    return static_cast<long>(h) * 3600 + m * 60 + (n == 3 ? sec : 0);
}

// "*Formatted" de Woffu ({"values": ["-7", "30"]}) a segundos.
long formattedSeconds(JsonVariantConst fmt) {
    JsonArrayConst values = fmt["values"];
    if (values.isNull() || values.size() == 0) {
        return 0;
    }
    String head = values[0].as<String>();
    head.trim();
    bool negative = head.startsWith("-");
    long hours = labs(head.toInt());
    long minutes = values.size() > 1 ? labs(values[1].as<String>().toInt()) : 0;
    long total = hours * 3600 + minutes * 60;
    return negative ? -total : total;
}

String urlEncode(const String& value) {
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            encoded += buf;
        }
    }
    return encoded;
}
}  // namespace

void WoffuClient::begin(const String& username, const String& password) {
    username_ = username;
    password_ = password;
    accessToken_ = "";
    userId_ = "";
    diariesHost_ = "app.woffu.com";
    diariesHostDiscovered_ = false;
    today_ = TodayWork();
    credentialsInvalid_ = false;
}

bool WoffuClient::login() {
    WiFiClientSecure client;
    applyCertBundle(client);

    HTTPClient http;
    if (!http.begin(client, kAuthUrl)) {
        logPrintf("Woffu API: no se pudo iniciar la conexion a %s\n", kAuthUrl);
        return false;
    }
    // Fuerza HTTP/1.0: evita que el servidor responda con Transfer-Encoding
    // chunked, que getStream() no decodifica y rompe deserializeJson.
    http.useHTTP10(true);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "grant_type=password&username=" + urlEncode(username_) + "&password=" + urlEncode(password_);
    int status = http.POST(body);
    logPrintf("Woffu API: POST %s -> %d\n", kAuthUrl, status);
    if (status == HTTP_CODE_BAD_REQUEST || status == HTTP_CODE_UNAUTHORIZED) {
        http.end();
        credentialsInvalid_ = true;
        logPrintln("Woffu API: usuario o password incorrectos (revisa la configuracion en el portal).");
        return false;
    }
    if (status != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();
    if (error) {
        logPrintf("Woffu API: error al parsear la respuesta de login (%s)\n", error.c_str());
        return false;
    }
    if (!doc["accessToken"].is<const char*>()) {
        logPrintln("Woffu API: la respuesta de login no incluye accessToken");
        return false;
    }

    accessToken_ = doc["accessToken"].as<const char*>();
    credentialsInvalid_ = false;
    return true;
}

bool WoffuClient::authenticatedGet(const String& url, JsonDocument& doc, const JsonDocument* filter,
                                   int* statusOut) {
    if (accessToken_.isEmpty() && !login()) {
        return false;
    }

    // Un único reintento tras 401 para refrescar el token (login de larga duración,
    // no es el backoff de reintentos por fallo de red/API que descarta Requisitos.md).
    for (int attempt = 0; attempt < 2; attempt++) {
        WiFiClientSecure client;
        applyCertBundle(client);

        HTTPClient http;
        if (!http.begin(client, url)) {
            logPrintf("Woffu API: no se pudo iniciar la conexion a %s\n", url.c_str());
            return false;
        }
        http.useHTTP10(true);
        http.addHeader("Authorization", "Bearer " + accessToken_);

        int status = http.GET();
        if (statusOut != nullptr) {
            *statusOut = status;
        }
        logPrintf("Woffu API: GET %s -> %d\n", url.c_str(), status);
        if (status == HTTP_CODE_UNAUTHORIZED) {
            http.end();
            if (!login()) {
                return false;
            }
            continue;
        }
        if (status != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        DeserializationError error =
            filter != nullptr
                ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                : deserializeJson(doc, http.getStream());
        http.end();
        if (error) {
            logPrintf("Woffu API: error al parsear la respuesta de %s (%s)\n", url.c_str(), error.c_str());
            return false;
        }
        return true;
    }

    return false;
}

bool WoffuClient::authenticatedPostJson(const String& url, const String& jsonBody) {
    if (accessToken_.isEmpty() && !login()) {
        return false;
    }

    // Mismo patron de reintento tras 401 que authenticatedGet().
    for (int attempt = 0; attempt < 2; attempt++) {
        WiFiClientSecure client;
        applyCertBundle(client);

        HTTPClient http;
        if (!http.begin(client, url)) {
            logPrintf("Woffu API: no se pudo iniciar la conexion a %s\n", url.c_str());
            return false;
        }
        http.useHTTP10(true);
        http.addHeader("Authorization", "Bearer " + accessToken_);
        http.addHeader("Content-Type", "application/json");

        int status = http.POST(jsonBody);
        logPrintf("Woffu API: POST %s -> %d\n", url.c_str(), status);
        if (status == HTTP_CODE_UNAUTHORIZED) {
            http.end();
            if (!login()) {
                return false;
            }
            continue;
        }
        http.end();
        return status >= 200 && status < 300;
    }

    return false;
}

bool WoffuClient::toggleSign() {
    // El servidor infiere el usuario del token (no hace falta UserId aqui, a
    // diferencia de /diarysummaries/workday) y decide fichar vs desfichar
    // segun el ultimo estado registrado - mismo body en ambos sentidos.
    String body = "{\"agreementEventId\":null,\"requestId\":null,\"deviceId\":\"WebApp\","
                  "\"latitude\":null,\"longitude\":null,\"timezoneOffset\":" +
                  String(timezoneOffsetMinutes()) + "}";
    return authenticatedPostJson(kSignsUrl, body);
}

WoffuStatus WoffuClient::fetchStatus() {
    JsonDocument doc;
    if (!authenticatedGet(kSlotsUrl, doc)) {
        return WoffuStatus::UNKNOWN;
    }
    if (!doc.is<JsonArray>()) {
        logPrintln("Woffu API: la respuesta de slots no es un array JSON");
        return WoffuStatus::UNKNOWN;
    }

    JsonArray slots = doc.as<JsonArray>();
    updateTodayWork(slots);
    if (slots.size() == 0) {
        return WoffuStatus::CLOCKED_OUT;
    }

    JsonObject lastSlot = slots[slots.size() - 1];
    bool hasOut = !lastSlot["out"].isNull();
    return hasOut ? WoffuStatus::CLOCKED_OUT : WoffuStatus::CLOCKED_IN;
}

bool WoffuClient::fetchUserId() {
    JsonDocument doc;
    if (!authenticatedGet(kUsersUrl, doc)) {
        return false;
    }
    if (!doc["UserId"].is<long>()) {
        logPrintln("Woffu API: la respuesta de /users no incluye UserId");
        return false;
    }
    userId_ = String(doc["UserId"].as<long>());
    return true;
}

bool WoffuClient::fetchWorkday(WorkdayInfo& out) {
    if (userId_.isEmpty() && !fetchUserId()) {
        return false;
    }

    String url = String(kWorkdayUrlPrefix) + userId_ + kWorkdayUrlSuffix;
    JsonDocument doc;
    if (!authenticatedGet(url, doc)) {
        return false;
    }

    out.isWeekend = doc["isWeekend"] | false;
    out.isHoliday = doc["isHoliday"] | false;

    return true;
}

void WoffuClient::updateTodayWork(JsonArrayConst slots) {
    // Los slots son los fichajes de hoy, con la hora local de la empresa
    // (la misma del dispositivo, ver TimeSync). Ver tools/balance.py.
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    long nowSec = static_cast<long>(local.tm_hour) * 3600 + local.tm_min * 60 + local.tm_sec;

    TodayWork work;
    work.valid = true;
    for (JsonObjectConst slot : slots) {
        long start = slotSeconds(slot["in"]);
        if (start < 0) {
            continue;
        }
        work.anySlot = true;
        long end = slotSeconds(slot["out"]);
        if (end < 0) {
            work.openSinceSec = static_cast<int32_t>(start);
            continue;
        }
        if (end > start) {
            work.closedSeconds += static_cast<uint32_t>(end - start);
        }
    }
    // Un tramo "abierto" que empieza en el futuro seria un dato raro: se ignora.
    if (work.openSinceSec > nowSec) {
        work.openSinceSec = -1;
    }
    today_ = work;
}

bool WoffuClient::discoverDiariesHost() {
    // Igual que tools/balance.py: si app.woffu.com no sirve los diarios, se
    // prueba con el subdominio de la empresa que devuelve /api/companies.
    diariesHostDiscovered_ = true;
    JsonDocument doc;
    if (!authenticatedGet(kCompaniesUrl, doc)) {
        return false;
    }
    JsonVariantConst entry = doc.is<JsonArray>() ? doc[0].as<JsonVariantConst>() : doc.as<JsonVariantConst>();
    for (const char* key : {"Domain", "domain", "CompanyDomain", "Url", "url"}) {
        if (!entry[key].is<const char*>()) {
            continue;
        }
        String host = entry[key].as<String>();
        host.trim();
        host.replace("https://", "");
        host.replace("http://", "");
        while (host.endsWith("/")) {
            host.remove(host.length() - 1);
        }
        if (host.isEmpty()) {
            continue;
        }
        if (host.indexOf('.') < 0) {
            host += ".woffu.com";
        }
        logPrintf("Woffu API: diarios en %s.\n", host.c_str());
        diariesHost_ = host;
        return true;
    }
    logPrintln("Woffu API: /api/companies no indica el subdominio de la empresa.");
    return false;
}

bool WoffuClient::fetchDiariesTotals(const String& fromDate, const String& toDate, int days, DiariesTotals& out) {
    if (userId_.isEmpty() && !fetchUserId()) {
        return false;
    }

    // La respuesta trae ademas un diario por dia (cientos de KB a final de
    // ano): el filtro los descarta mientras se lee, solo quedan los totales.
    // pageSize como el de la web (todos los dias del rango en una pagina).
    JsonDocument filter;
    filter["totalDifferenceTime"] = true;
    filter["totalScheduleTimeFormatted"] = true;

    for (int attempt = 0; attempt < 2; attempt++) {
        String url = "https://" + diariesHost_ + "/api/svc/core/diariesquery/users/" + userId_ +
                     "/diaries/summary/presence?userId=" + userId_ + "&fromDate=" + fromDate +
                     "&toDate=" + toDate + "&pageSize=" + String(days + 1) +
                     "&includeNotHourTypes=true&includeDifference=true";
        JsonDocument doc;
        int status = 0;
        if (authenticatedGet(url, doc, &filter, &status)) {
            if (!doc["totalDifferenceTime"].is<long>()) {
                logPrintln("Woffu API: la respuesta de diarios no incluye totalDifferenceTime");
                return false;
            }
            out.differenceSeconds = doc["totalDifferenceTime"].as<long>();
            out.scheduleSeconds = formattedSeconds(doc["totalScheduleTimeFormatted"]);
            return true;
        }
        bool wrongHost = status == HTTP_CODE_FORBIDDEN || status == HTTP_CODE_NOT_FOUND;
        if (!wrongHost || diariesHostDiscovered_ || !discoverDiariesHost()) {
            return false;
        }
    }
    return false;
}
