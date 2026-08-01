#include "OtaUpdater.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "net/CertBundle.h"
#include "Version.h"

namespace {
constexpr const char* kVersionUrl =
    "https://github.com/emanuele-dedonatis/semaforo-woffu/releases/latest/download/version.txt";
constexpr const char* kFirmwareUrl =
    "https://github.com/emanuele-dedonatis/semaforo-woffu/releases/latest/download/firmware.bin";

// HTTPClient::errorToString(-1) siempre dice "connection refused" tanto si
// fallo el TCP connect como si fallo el handshake TLS (cert bundle, memoria,
// etc.): WiFiClientSecure::lastError() guarda el motivo mbedtls real cuando
// lo hay, asi que lo anexamos si esta disponible.
String tlsErrorDetail(WiFiClientSecure& client) {
    char buf[100];
    int code = client.lastError(buf, sizeof(buf));
    return code != 0 ? " (TLS: " + String(buf) + ")" : "";
}
}  // namespace

OtaResult OtaUpdater::checkAndUpdate() {
    lastErrorDetail_ = "";

    WiFiClientSecure versionClient;
    applyCertBundle(versionClient);

    HTTPClient http;
    if (!http.begin(versionClient, kVersionUrl)) {
        lastErrorDetail_ = "No se pudo iniciar la conexion para comprobar version.txt.";
        return OtaResult::ERROR;
    }
    // GitHub redirige releases/latest/download/* (varios saltos, hasta el CDN
    // final); sin esto HTTPClient no sigue el 302 y GET() devuelve ese codigo
    // en vez del contenido.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        lastErrorDetail_ = status > 0
            ? "Comprobando version.txt: HTTP " + String(status)
            : "Comprobando version.txt: " + HTTPClient::errorToString(status) + tlsErrorDetail(versionClient);
        http.end();
        return OtaResult::ERROR;
    }
    String latestVersion = http.getString();
    latestVersion.trim();
    http.end();

    if (latestVersion == FIRMWARE_VERSION) {
        return OtaResult::UP_TO_DATE;
    }

    WiFiClientSecure updateClient;
    applyCertBundle(updateClient);

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    switch (httpUpdate.update(updateClient, kFirmwareUrl)) {
        case HTTP_UPDATE_OK:
            return OtaResult::UPDATED;
        case HTTP_UPDATE_NO_UPDATES:
            return OtaResult::UP_TO_DATE;
        default:
            lastErrorDetail_ =
                "Descargando firmware.bin: " + httpUpdate.getLastErrorString() + tlsErrorDetail(updateClient);
            return OtaResult::ERROR;
    }
}
