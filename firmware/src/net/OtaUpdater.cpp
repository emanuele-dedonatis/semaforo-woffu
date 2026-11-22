#include "OtaUpdater.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "Log.h"
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

bool OtaUpdater::fetchLatestVersion(String& out) {
    WiFiClientSecure versionClient;
    applyCertBundle(versionClient);

    HTTPClient http;
    if (!http.begin(versionClient, kVersionUrl)) {
        lastErrorDetail_ = "No se pudo iniciar la conexion para comprobar version.txt.";
        return false;
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
        return false;
    }
    out = http.getString();
    out.trim();
    http.end();
    return true;
}

OtaCheckResult OtaUpdater::checkForUpdate(String& latestVersionOut) {
    lastErrorDetail_ = "";

    if (!fetchLatestVersion(latestVersionOut)) {
        return OtaCheckResult::ERROR;
    }
    return latestVersionOut == FIRMWARE_VERSION ? OtaCheckResult::UP_TO_DATE : OtaCheckResult::AVAILABLE;
}

OtaResult OtaUpdater::update(const String& targetVersion) {
    lastErrorDetail_ = "";

    if (targetVersion == FIRMWARE_VERSION) {
        return OtaResult::UP_TO_DATE;
    }

    // httpUpdate.update() reinicia el dispositivo el mismo si la descarga e
    // instalacion tienen exito (rebootOnUpdate(true) mas abajo): este es el
    // ultimo punto donde nos da tiempo a dejar constancia (Serial y, via el
    // callback, algo que sobreviva al reboot para el portal).
    logPrintf("OTA: version nueva disponible (%s -> %s). Descargando e instalando...\n", FIRMWARE_VERSION,
              targetVersion.c_str());
    if (beforeFlash_) {
        beforeFlash_(FIRMWARE_VERSION, targetVersion);
    }

    WiFiClientSecure updateClient;
    applyCertBundle(updateClient);

    if (progress_) {
        httpUpdate.onProgress([this](int current, int total) {
            progress_(static_cast<size_t>(current), static_cast<size_t>(total));
        });
    }
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
