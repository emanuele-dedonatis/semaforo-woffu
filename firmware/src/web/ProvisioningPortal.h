#pragma once

#include <Arduino.h>
#include "config/Config.h"

class WebServer;
class DNSServer;

// Estado del widget de comprobacion/actualizacion OTA de la pagina principal
// (independiente del aviso estatico post-reboot, ver otaStatusMessage_).
enum class OtaUiState : uint8_t { IDLE, CHECKING, UP_TO_DATE, AVAILABLE, UPDATING, ERROR };

// Estado del widget de aprendizaje de tarjeta NFC, mismo patron que OtaUiState.
enum class NfcLearnUiState : uint8_t { IDLE, WAITING, SUCCESS, TIMEOUT, ERROR };

class ProvisioningPortal {
public:
    void begin(const DeviceConfig& current, bool hasLearnedCard);
    void loop();
    void stop();
    bool hasClient() const;

    bool takeConfigToSave(DeviceConfig& out);
    bool takeOtaUpdateRequested();
    bool takeFactoryResetRequested();
    void reportOtaStatus(const String& message);

    // Version objetivo de la ultima comprobacion con exito (la que ofrece el
    // boton "Actualizar"); solo tiene sentido leerla tras un reportOtaAvailable().
    String otaTargetVersion() const { return otaLatestVersion_; }

    void reportOtaChecking();
    void reportOtaUpToDate();
    void reportOtaAvailable(const String& latestVersion);
    void reportOtaProgress(size_t current, size_t total);
    void reportOtaError(const String& message);

    bool takeNfcLearnRequested();
    // uidHex completo solo se usa para calcular la mascara mostrada en la
    // pagina (p.ej. "04..C3"); nunca se expone en claro.
    void reportNfcLearnSuccess(const String& uidHex);
    void reportNfcLearnTimeout();
    void reportNfcLearnError(const String& message);

private:
    void handleRoot();
    void handleSave();
    void handleOtaUpdate();
    void handleFactoryReset();
    void handleNfcLearn();
    void handleNotFound();
    String renderOtaNotice();
    String renderNfcNotice();

    WebServer* server_ = nullptr;
    DNSServer* dns_ = nullptr;
    DeviceConfig current_;

    bool pendingSave_ = false;
    DeviceConfig pendingConfig_;
    bool pendingOtaUpdate_ = false;
    bool pendingFactoryReset_ = false;
    String otaStatusMessage_;
    String ssidOptionsHtml_;

    OtaUiState otaUiState_ = OtaUiState::IDLE;
    String otaLatestVersion_;
    String otaErrorMessage_;
    uint8_t otaProgressPercent_ = 0;

    bool pendingNfcLearn_ = false;
    NfcLearnUiState nfcLearnUiState_ = NfcLearnUiState::IDLE;
    bool hasLearnedCard_ = false;
    String nfcLearnUidMasked_;
    String nfcLearnErrorMessage_;
};
