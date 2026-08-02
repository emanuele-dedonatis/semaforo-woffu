#include "WoffuClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "net/CertBundle.h"

namespace {
constexpr const char* kAuthUrl = "https://app.woffu.com/api/svc/accounts/authorization/token";
constexpr const char* kSlotsUrl = "https://app.woffu.com/api/svc/signs/v2/signs/slots";

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
}

bool WoffuClient::login() {
    WiFiClientSecure client;
    applyCertBundle(client);

    HTTPClient http;
    if (!http.begin(client, kAuthUrl)) {
        Serial.printf("Woffu API: no se pudo iniciar la conexion a %s\n", kAuthUrl);
        return false;
    }
    // Fuerza HTTP/1.0: evita que el servidor responda con Transfer-Encoding
    // chunked, que getStream() no decodifica y rompe deserializeJson.
    http.useHTTP10(true);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "grant_type=password&username=" + urlEncode(username_) + "&password=" + urlEncode(password_);
    int status = http.POST(body);
    Serial.printf("Woffu API: POST %s -> %d\n", kAuthUrl, status);
    if (status != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();
    if (error) {
        Serial.printf("Woffu API: error al parsear la respuesta de login (%s)\n", error.c_str());
        return false;
    }
    if (!doc["accessToken"].is<const char*>()) {
        Serial.println("Woffu API: la respuesta de login no incluye accessToken");
        return false;
    }

    accessToken_ = doc["accessToken"].as<const char*>();
    return true;
}

WoffuStatus WoffuClient::fetchStatus() {
    if (accessToken_.isEmpty() && !login()) {
        return WoffuStatus::UNKNOWN;
    }

    // Un único reintento tras 401 para refrescar el token (login de larga duración,
    // no es el backoff de reintentos por fallo de red/API que descarta Requisitos.md).
    for (int attempt = 0; attempt < 2; attempt++) {
        WiFiClientSecure client;
        applyCertBundle(client);

        HTTPClient http;
        if (!http.begin(client, kSlotsUrl)) {
            Serial.printf("Woffu API: no se pudo iniciar la conexion a %s\n", kSlotsUrl);
            return WoffuStatus::UNKNOWN;
        }
        http.useHTTP10(true);
        http.addHeader("Authorization", "Bearer " + accessToken_);

        int status = http.GET();
        Serial.printf("Woffu API: GET %s -> %d\n", kSlotsUrl, status);
        if (status == HTTP_CODE_UNAUTHORIZED) {
            http.end();
            if (!login()) {
                return WoffuStatus::UNKNOWN;
            }
            continue;
        }
        if (status != HTTP_CODE_OK) {
            http.end();
            return WoffuStatus::UNKNOWN;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream());
        http.end();
        if (error) {
            Serial.printf("Woffu API: error al parsear la respuesta de slots (%s)\n", error.c_str());
            return WoffuStatus::UNKNOWN;
        }
        if (!doc.is<JsonArray>()) {
            Serial.println("Woffu API: la respuesta de slots no es un array JSON");
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

    return WoffuStatus::UNKNOWN;
}
