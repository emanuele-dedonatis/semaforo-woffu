#include "ProvisioningPortal.h"

#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_mac.h>
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

const char kPageTemplate[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Semaforo Woffu</title>
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
</style></head><body>
<h1>Configurar Semaforo Woffu</h1>
<p class="version">Firmware %VERSION%</p>
%OTA_STATUS%
<form method="POST" action="/save">
<label>WiFi SSID</label><input name="ssid" value="%SSID%" required>
<label>WiFi Password</label><input name="wifi_pass" type="password" value="%WIFI_PASS%">
<label>Usuario Woffu</label><input name="woffu_user" value="%WOFFU_USER%" required>
<label>Password Woffu</label><input name="woffu_pass" type="password" value="%WOFFU_PASS%">
<label>Zona horaria (TZ POSIX)</label><input name="tz" value="%TZ%">
<div class="row">
<div><label>Entrada inicio</label><input name="win_in_start" type="time" value="%WIN_IN_START%"></div>
<div><label>Entrada fin</label><input name="win_in_end" type="time" value="%WIN_IN_END%"></div>
</div>
<div class="row">
<div><label>Salida inicio</label><input name="win_out_start" type="time" value="%WIN_OUT_START%"></div>
<div><label>Salida fin</label><input name="win_out_end" type="time" value="%WIN_OUT_END%"></div>
</div>
<div class="row">
<div><label>Polling activo (s)</label><input name="poll_active_s" type="number" min="5" value="%POLL_ACTIVE%"></div>
<div><label>Polling pasivo (s)</label><input name="poll_passive_s" type="number" min="30" value="%POLL_PASSIVE%"></div>
</div>
<label>Brillo LEDs (0-255)</label><input name="brightness" type="number" min="0" max="255" value="%BRIGHTNESS%">
<div class="checkbox-row"><input name="force_active" type="checkbox" id="force_active" %FORCE_ACTIVE_CHECKED%><label for="force_active">Forzar ventana activa (pruebas, ignora horario y fin de semana)</label></div>
<button type="submit">Guardar y reiniciar</button>
</form>
<hr>
<form method="POST" action="/ota"><button>Comprobar actualizacion OTA</button></form>
<form method="POST" action="/factory-reset" onsubmit="return confirm('Seguro? Borra toda la configuracion.')">
<button>Restablecer de fabrica</button></form>
</body></html>)HTML";

const char kSavedPage[] =
    "<!doctype html><html><body><h1>Guardado</h1><p>Reiniciando...</p></body></html>";
const char kOtaPage[] =
    "<!doctype html><html><head><meta http-equiv=\"refresh\" content=\"5;url=/\"></head>"
    "<body><h1>Comprobando...</h1><p>Comprobando actualizacion OTA. Esta pagina se "
    "recargara sola; si la comprobacion tarda, espera y vuelve a cargar /.</p></body></html>";
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
    Serial.printf("Portal WiFi: %s, password: %s (192.168.4.1)\n", apSsid, apPassword);

    dns_ = new DNSServer();
    dns_->start(kDnsPort, "*", WiFi.softAPIP());

    server_ = new WebServer(kHttpPort);
    server_->on("/", HTTP_GET, [this]() { handleRoot(); });
    server_->on("/save", HTTP_POST, [this]() { handleSave(); });
    server_->on("/ota", HTTP_POST, [this]() { handleOta(); });
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

bool ProvisioningPortal::takeOtaRequested() {
    bool value = pendingOta_;
    pendingOta_ = false;
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

void ProvisioningPortal::handleRoot() {
    String page(kPageTemplate);
    page.replace("%VERSION%", FIRMWARE_VERSION);
    page.replace("%OTA_STATUS%", otaStatusMessage_.isEmpty()
        ? ""
        : "<p class=\"notice\">" + htmlEscape(otaStatusMessage_) + "</p>");
    page.replace("%SSID%", htmlEscape(current_.wifiSsid));
    page.replace("%WIFI_PASS%", htmlEscape(current_.wifiPassword));
    page.replace("%WOFFU_USER%", htmlEscape(current_.woffuUsername));
    page.replace("%WOFFU_PASS%", htmlEscape(current_.woffuPassword));
    page.replace("%TZ%", htmlEscape(current_.timezone));
    page.replace("%WIN_IN_START%", minutesToHhMm(current_.windowIn.startMinutes));
    page.replace("%WIN_IN_END%", minutesToHhMm(current_.windowIn.endMinutes));
    page.replace("%WIN_OUT_START%", minutesToHhMm(current_.windowOut.startMinutes));
    page.replace("%WIN_OUT_END%", minutesToHhMm(current_.windowOut.endMinutes));
    page.replace("%POLL_ACTIVE%", String(current_.pollActiveSeconds));
    page.replace("%POLL_PASSIVE%", String(current_.pollPassiveSeconds));
    page.replace("%BRIGHTNESS%", String(current_.brightness));
    page.replace("%FORCE_ACTIVE_CHECKED%", current_.forceActiveWindow ? "checked" : "");
    server_->send(200, "text/html", page);
}

void ProvisioningPortal::handleSave() {
    DeviceConfig config = current_;
    config.wifiSsid = server_->arg("ssid");
    config.wifiPassword = server_->arg("wifi_pass");
    config.woffuUsername = server_->arg("woffu_user");
    config.woffuPassword = server_->arg("woffu_pass");
    config.timezone = server_->arg("tz");

    bool ok = true;
    ok &= parseHhMm(server_->arg("win_in_start"), config.windowIn.startMinutes);
    ok &= parseHhMm(server_->arg("win_in_end"), config.windowIn.endMinutes);
    ok &= parseHhMm(server_->arg("win_out_start"), config.windowOut.startMinutes);
    ok &= parseHhMm(server_->arg("win_out_end"), config.windowOut.endMinutes);

    if (!ok || config.wifiSsid.isEmpty() || config.woffuUsername.isEmpty()) {
        server_->send(400, "text/html", "<!doctype html><html><body><h1>Error</h1><p>Revisa los campos.</p></body></html>");
        return;
    }

    config.pollActiveSeconds = static_cast<uint16_t>(server_->arg("poll_active_s").toInt());
    config.pollPassiveSeconds = static_cast<uint16_t>(server_->arg("poll_passive_s").toInt());
    config.brightness = static_cast<uint8_t>(server_->arg("brightness").toInt());
    config.forceActiveWindow = server_->hasArg("force_active");

    pendingConfig_ = config;
    pendingSave_ = true;
    server_->send(200, "text/html", kSavedPage);
}

void ProvisioningPortal::handleOta() {
    pendingOta_ = true;
    server_->send(200, "text/html", kOtaPage);
}

void ProvisioningPortal::handleFactoryReset() {
    pendingFactoryReset_ = true;
    server_->send(200, "text/html", kFactoryResetPage);
}

void ProvisioningPortal::handleNotFound() {
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
}
