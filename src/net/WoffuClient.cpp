#include "WoffuClient.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "Log.h"
#include "net/CertBundle.h"

namespace {
constexpr const char* kAuthUrl = "https://app.woffu.com/api/svc/accounts/authorization/token";
constexpr const char* kSlotsUrl = "https://app.woffu.com/api/svc/signs/v2/signs/slots";
constexpr const char* kUsersUrl = "https://app.woffu.com/api/users";
constexpr const char* kWorkdayUrlPrefix = "https://app.woffu.com/api/svc/core/users/";
constexpr const char* kWorkdayUrlSuffix = "/diarysummaries/workday";

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

// Convierte "HH:MM:SS" a minutos del dia. Devuelve false si el formato no es valido.
bool parseHhMmSs(const String& value, uint16_t& outMinutes) {
    if (value.length() < 5) {
        return false;
    }
    int hh = value.substring(0, 2).toInt();
    int mm = value.substring(3, 5).toInt();
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        return false;
    }
    outMinutes = static_cast<uint16_t>(hh * 60 + mm);
    return true;
}
}  // namespace

void WoffuClient::begin(const String& username, const String& password) {
    username_ = username;
    password_ = password;
    accessToken_ = "";
    userId_ = "";
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

bool WoffuClient::authenticatedGet(const String& url, JsonDocument& doc) {
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

        DeserializationError error = deserializeJson(doc, http.getStream());
        http.end();
        if (error) {
            logPrintf("Woffu API: error al parsear la respuesta de %s (%s)\n", url.c_str(), error.c_str());
            return false;
        }
        return true;
    }

    return false;
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

    const char* startTime = doc["startTime"] | "";
    const char* endTime = doc["endTime"] | "";
    bool timesOk = parseHhMmSs(startTime, out.startMinutes) && parseHhMmSs(endTime, out.endMinutes);
    if (!timesOk) {
        out.startMinutes = 0;
        out.endMinutes = 0;
        // En fin de semana/festivo Woffu puede no devolver horario, no hace falta para decidir OFF.
        if (!out.isWeekend && !out.isHoliday) {
            logPrintln("Woffu API: la respuesta de workday no incluye startTime/endTime validos");
            return false;
        }
    }

    return true;
}
