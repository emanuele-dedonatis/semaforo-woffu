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
}  // namespace

OtaResult OtaUpdater::checkAndUpdate() {
    WiFiClientSecure versionClient;
    applyCertBundle(versionClient);

    HTTPClient http;
    if (!http.begin(versionClient, kVersionUrl)) {
        return OtaResult::ERROR;
    }
    int status = http.GET();
    if (status != HTTP_CODE_OK) {
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
    switch (httpUpdate.update(updateClient, kFirmwareUrl)) {
        case HTTP_UPDATE_OK:
            return OtaResult::UPDATED;
        case HTTP_UPDATE_NO_UPDATES:
            return OtaResult::UP_TO_DATE;
        default:
            return OtaResult::ERROR;
    }
}
