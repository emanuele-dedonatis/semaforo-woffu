#include "ProvisioningPortal.h"

#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include "Log.h"
#include "Version.h"

namespace {
constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kHttpPort = 80;

String htmlEscape(const String& text) {
    String out;
    out.reserve(text.length());
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c; break;
        }
    }
    return out;
}

bool parseHhMm(const String& text, uint16_t& outMinutes) {
    unsigned int h = 0;
    unsigned int m = 0;
    if (sscanf(text.c_str(), "%u:%u", &h, &m) != 2 || h > 23 || m > 59) {
        return false;
    }
    outMinutes = static_cast<uint16_t>(h * 60 + m);
    return true;
}

String minutesToHhMm(uint16_t minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02u:%02u", minutes / 60, minutes % 60);
    return String(buf);
}

// Escanea redes WiFi visibles y arma las <option> de un <select>, ordenadas
// por senal (RSSI) descendente y sin SSIDs duplicados (redes con varios APs).
// Se usa <select> en vez de <datalist> porque el navegador cautivo que abren
// iOS/Android al conectarse al AP (CNA / popup "Iniciar sesion en red") es un
// WebView muy limitado: normalmente no ejecuta JavaScript y el soporte de
// <datalist> es pobre o nulo, pero <select> es un control nativo universal.
String scanSsidOptionsHtml(const String& currentSsid) {
    constexpr int kMaxNetworks = 32;
    int count = WiFi.scanNetworks();
    if (count <= 0) {
        WiFi.scanDelete();
        return "";
    }
    if (count > kMaxNetworks) {
        count = kMaxNetworks;
    }

    int order[kMaxNetworks];
    for (int i = 0; i < count; i++) {
        order[i] = i;
    }
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (WiFi.RSSI(order[j]) > WiFi.RSSI(order[i])) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    String options;
    String seen[kMaxNetworks];
    int seenCount = 0;
    for (int k = 0; k < count; k++) {
        String ssid = WiFi.SSID(order[k]);
        if (ssid.isEmpty()) {
            continue;
        }
        bool duplicate = false;
        for (int s = 0; s < seenCount; s++) {
            if (seen[s] == ssid) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        seen[seenCount++] = ssid;
        bool isCurrent = (ssid == currentSsid);
        String escaped = htmlEscape(ssid);
        options += "<option value=\"" + escaped + "\"" + (isCurrent ? " selected" : "") + ">" + escaped + "</option>";
    }
    WiFi.scanDelete();
    return options;
}

// Estados en los que conviene recargar sola la pagina (sin JavaScript, para
// que funcione tambien en el navegador cautivo restringido que abren
// iOS/Android al conectarse al AP, ver scanSsidOptionsHtml() mas arriba).
String otaRefreshMetaFor(OtaUiState state) {
    switch (state) {
        case OtaUiState::IDLE:      // sin wifi todavia: reintenta hasta que conecte
        case OtaUiState::CHECKING:
        case OtaUiState::UPDATING:
            return "<meta http-equiv=\"refresh\" content=\"3;url=/\">";
        default:
            return "";
    }
}

const char kPageTemplate[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Semaforo Woffu</title>
%OTA_REFRESH_META%
<style>
body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em;color:#222}
label{display:block;margin-top:1em;font-weight:bold}
input{width:100%;padding:.5em;box-sizing:border-box;font-size:1em}
button{margin-top:1.5em;padding:.7em 1.5em;font-size:1em}
.row{display:flex;gap:1em}.row>div{flex:1}
.checkbox-row{display:flex;align-items:center;gap:.5em;margin-top:1em}
.checkbox-row input{width:auto}
.checkbox-row label{margin-top:0}
.version{color:#666;font-size:.85em;margin-top:-.5em}
.notice{background:#eef;padding:.6em 1em;border-radius:4px;margin-top:1em}
select{width:100%;padding:.5em;box-sizing:border-box;font-size:1em}
</style></head><body>
<h1>Configurar Semaforo Woffu</h1>
<p class="version">Firmware %VERSION%</p>
%OTA_STATUS%
%OTA_WIDGET%
<form method="POST" action="/save">
<label>WiFi SSID</label>
<select name="ssid_select">
<option value="">-- Selecciona una red detectada --</option>
%SSID_OPTIONS%
</select>
<label>WiFi Password</label><input name="wifi_pass" type="password" value="%WIFI_PASS%">
<label>Usuario Woffu</label><input name="woffu_user" value="%WOFFU_USER%" required>
<label>Password Woffu</label><input name="woffu_pass" type="password" value="%WOFFU_PASS%">
<div class="row">
<div><label>Encendido</label><input name="win_start" type="time" value="%WIN_START%"></div>
<div><label>Apagado</label><input name="win_end" type="time" value="%WIN_END%"></div>
</div>
<div class="checkbox-row"><input name="force_active" type="checkbox" id="force_active" %FORCE_ACTIVE_CHECKED%><label for="force_active">Forzar ventana activa (pruebas, ignora horario y jornada de Woffu)</label></div>
<button type="submit">Guardar y reiniciar</button>
</form>
<hr>
<form method="POST" action="/factory-reset" onsubmit="return confirm('Seguro? Borra toda la configuracion.')">
<button>Restablecer de fabrica</button></form>
</body></html>)HTML";

const char kSavedPage[] =
    "<!doctype html><html><body><h1>Guardado</h1><p>Reiniciando...</p></body></html>";
const char kFactoryResetPage[] =
    "<!doctype html><html><body><h1>OK</h1><p>Restableciendo de fabrica y reiniciando...</p></body></html>";
}

void ProvisioningPortal::begin(const DeviceConfig& current) {
    current_ = current;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char apSsid[24];
    snprintf(apSsid, sizeof(apSsid), "Semaforo-%02X%02X%02X", mac[3], mac[4], mac[5]);

    uint32_t passValue = 0;
    for (uint8_t b : mac) {
        passValue = (passValue * 31) + b;
    }
    char apPassword[9];
    snprintf(apPassword, sizeof(apPassword), "%08u", passValue % 100000000UL);

    WiFi.softAP(apSsid, apPassword);
    logPrintf("Portal WiFi: %s, password: %s (192.168.4.1)\n", apSsid, apPassword);

    ssidOptionsHtml_ = scanSsidOptionsHtml(current_.wifiSsid);

    dns_ = new DNSServer();
    dns_->start(kDnsPort, "*", WiFi.softAPIP());

    server_ = new WebServer(kHttpPort);
    server_->on("/", HTTP_GET, [this]() { handleRoot(); });
    server_->on("/save", HTTP_POST, [this]() { handleSave(); });
    server_->on("/ota/update", HTTP_POST, [this]() { handleOtaUpdate(); });
    server_->on("/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
    server_->onNotFound([this]() { handleNotFound(); });
    server_->begin();
}

void ProvisioningPortal::loop() {
    if (dns_ != nullptr) {
        dns_->processNextRequest();
    }
    if (server_ != nullptr) {
        server_->handleClient();
    }
}

void ProvisioningPortal::stop() {
    if (server_ != nullptr) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
    if (dns_ != nullptr) {
        dns_->stop();
        delete dns_;
        dns_ = nullptr;
    }
    WiFi.softAPdisconnect(true);
}

bool ProvisioningPortal::hasClient() const {
    return WiFi.softAPgetStationNum() > 0;
}

bool ProvisioningPortal::takeConfigToSave(DeviceConfig& out) {
    if (!pendingSave_) {
        return false;
    }
    out = pendingConfig_;
    pendingSave_ = false;
    return true;
}

bool ProvisioningPortal::takeOtaUpdateRequested() {
    bool value = pendingOtaUpdate_;
    pendingOtaUpdate_ = false;
    return value;
}

bool ProvisioningPortal::takeFactoryResetRequested() {
    bool value = pendingFactoryReset_;
    pendingFactoryReset_ = false;
    return value;
}

void ProvisioningPortal::reportOtaStatus(const String& message) {
    otaStatusMessage_ = message;
}

void ProvisioningPortal::reportOtaChecking() {
    otaUiState_ = OtaUiState::CHECKING;
}

void ProvisioningPortal::reportOtaUpToDate() {
    otaUiState_ = OtaUiState::UP_TO_DATE;
}

void ProvisioningPortal::reportOtaAvailable(const String& latestVersion) {
    otaUiState_ = OtaUiState::AVAILABLE;
    otaLatestVersion_ = latestVersion;
}

void ProvisioningPortal::reportOtaProgress(size_t current, size_t total) {
    otaUiState_ = OtaUiState::UPDATING;
    otaProgressPercent_ = total > 0 ? static_cast<uint8_t>((current * 100) / total) : 0;
}

void ProvisioningPortal::reportOtaError(const String& message) {
    otaUiState_ = OtaUiState::ERROR;
    otaErrorMessage_ = message;
}

String ProvisioningPortal::renderOtaNotice() {
    switch (otaUiState_) {
        case OtaUiState::IDLE:
            return "<p class=\"notice\">Conectando y sincronizando hora todavia: en cuanto "
                   "termine se comprobaran las actualizaciones.</p>";
        case OtaUiState::CHECKING:
            return "<p class=\"notice\">Comprobando actualizaciones...</p>";
        case OtaUiState::UP_TO_DATE:
            return "<p class=\"notice\">Firmware al dia (v" + htmlEscape(FIRMWARE_VERSION) + ").</p>";
        case OtaUiState::AVAILABLE:
            return "<p class=\"notice\">Version disponible: v" + htmlEscape(otaLatestVersion_) + ".</p>"
                   "<form method=\"POST\" action=\"/ota/update\" onsubmit=\"return confirm('Se "
                   "descargara e instalara la actualizacion; el dispositivo se reiniciara solo. NO "
                   "desconectes el dispositivo de la corriente hasta que los LEDs vuelvan a "
                   "encenderse. Continuar?')\"><button>Actualizar</button></form>";
        case OtaUiState::UPDATING: {
            String detail = otaProgressPercent_ >= 100
                ? "finalizando instalacion, el dispositivo se reiniciara..."
                : "descargando firmware (" + String(otaProgressPercent_) + "%)...";
            return "<p class=\"notice\">Actualizando: " + detail + " Puede tardar unos segundos en "
                   "avanzar; no hace falta refrescar la pagina ni volver a pulsar el boton, se "
                   "actualiza sola. No desconectes el dispositivo de la corriente hasta que los "
                   "LEDs vuelvan a encenderse.</p>";
        }
        case OtaUiState::ERROR:
            return "<p class=\"notice\">No se pudo comprobar/instalar la actualizacion: " +
                   htmlEscape(otaErrorMessage_) + " Reinicia el dispositivo para reintentarlo.</p>";
    }
    return "";
}

void ProvisioningPortal::handleRoot() {
    // GET / es de solo lectura: la comprobacion OTA la dispara el orquestador
    // solo (AppStateMachine::performOtaCheck(), en cuanto hay wifi+hora), no
    // esta pagina. El progreso se ve recargando la propia pagina via
    // meta-refresh, sin JavaScript (ver comentario de scanSsidOptionsHtml()).
    String page(kPageTemplate);
    page.replace("%OTA_REFRESH_META%", otaRefreshMetaFor(otaUiState_));
    page.replace("%VERSION%", FIRMWARE_VERSION);
    page.replace("%OTA_STATUS%", otaStatusMessage_.isEmpty()
        ? ""
        : "<p class=\"notice\">" + htmlEscape(otaStatusMessage_) + "</p>");
    page.replace("%OTA_WIDGET%", renderOtaNotice());
    page.replace("%SSID_OPTIONS%", ssidOptionsHtml_);
    page.replace("%WIFI_PASS%", htmlEscape(current_.wifiPassword));
    page.replace("%WOFFU_USER%", htmlEscape(current_.woffuUsername));
    page.replace("%WOFFU_PASS%", htmlEscape(current_.woffuPassword));
    page.replace("%WIN_START%", minutesToHhMm(current_.activeWindow.startMinutes));
    page.replace("%WIN_END%", minutesToHhMm(current_.activeWindow.endMinutes));
    page.replace("%FORCE_ACTIVE_CHECKED%", current_.forceActiveWindow ? "checked" : "");
    server_->send(200, "text/html", page);
}

void ProvisioningPortal::handleSave() {
    DeviceConfig config = current_;
    config.wifiSsid = server_->arg("ssid_select");
    config.wifiPassword = server_->arg("wifi_pass");
    config.woffuUsername = server_->arg("woffu_user");
    config.woffuPassword = server_->arg("woffu_pass");

    bool ok = true;
    ok &= parseHhMm(server_->arg("win_start"), config.activeWindow.startMinutes);
    ok &= parseHhMm(server_->arg("win_end"), config.activeWindow.endMinutes);

    if (!ok || config.wifiSsid.isEmpty() || config.woffuUsername.isEmpty()) {
        server_->send(400, "text/html", "<!doctype html><html><body><h1>Error</h1><p>Revisa los campos.</p></body></html>");
        return;
    }

    config.forceActiveWindow = server_->hasArg("force_active");

    pendingConfig_ = config;
    pendingSave_ = true;
    server_->send(200, "text/html", kSavedPage);
}

void ProvisioningPortal::handleOtaUpdate() {
    pendingOtaUpdate_ = true;
    otaUiState_ = OtaUiState::UPDATING;
    otaProgressPercent_ = 0;
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
}

void ProvisioningPortal::handleFactoryReset() {
    pendingFactoryReset_ = true;
    server_->send(200, "text/html", kFactoryResetPage);
}

void ProvisioningPortal::handleNotFound() {
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
}
