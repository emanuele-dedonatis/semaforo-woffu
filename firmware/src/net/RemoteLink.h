#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class WebSocketsClient;

// Comando recibido del servidor remoto (ver Arquitectura.md, `## Control
// remoto` para el protocolo completo). `id` lo genera el servidor y se
// devuelve tal cual en el ack, para que la web app sepa a que boton responde.
struct RemoteCommand {
    String id;
    String cmd;    // sign_in, sign_out, refresh, led_set, led_auto, reboot, ota_update, status
    String color;  // solo led_set: red, yellow, green, all, off
    String mode;   // solo led_set: solid, blink_slow, blink_fast
};

// Cliente WebSocket (wss://) hacia el servidor de control remoto. Solo
// transporta: no decide nada sobre fichajes ni LEDs, eso lo hace
// AppStateMachine con los comandos que saca de aqui (takeCommand()), igual
// que hace con las banderas de ProvisioningPortal.
//
// Se bombea desde loopTask (loop()), sin tarea propia. Unico punto
// bloqueante: el handshake TCP+TLS de cada intento de conexion (se espacian
// cada vez mas mientras el servidor no responde, ver kReconnect*).
class RemoteLink {
public:
    // url vacia = deshabilitado (loop() no hace nada). user/password: los del
    // AP del portal (DeviceIdentity), enviados como HTTP Basic en el handshake.
    void begin(const String& url, const String& user, const String& password);
    // Solo debe llamarse con WiFi conectada y hora NTP sincronizada (sin hora
    // la validacion del certificado TLS falla, igual que con Woffu/GitHub).
    void loop();

    bool enabled() const { return enabled_; }
    bool connected() const;

    bool takeCommand(RemoteCommand& out);
    // true justo tras conectar, cada kStatusPeriodMs, o si se llamo a
    // requestStatus(): toca mandar un sendStatus() completo.
    bool takeStatusDue();
    void requestStatus() { statusRequested_ = true; }

    void sendStatus(JsonDocument& doc);
    void sendAck(const String& id, bool ok, const String& message);
    void sendEvent(const char* kind, const String& message);

private:
    void onEvent(int type, uint8_t* payload, size_t length);
    void handleText(const char* payload, size_t length);
    void flushLogs();
    bool sendJson(JsonDocument& doc);

    WebSocketsClient* ws_ = nullptr;
    bool enabled_ = false;
    bool started_ = false;
    bool connected_ = false;
    String host_;
    uint16_t port_ = 443;
    String path_;
    bool secure_ = true;
    String user_;
    String password_;

    uint32_t disconnectedSinceMs_ = 0;
    uint32_t currentReconnectMs_ = 0;
    uint32_t lastStatusMs_ = 0;
    bool statusRequested_ = false;
    uint32_t lastLogSeqSent_ = 0;

    static constexpr size_t kCommandQueueSize = 4;
    RemoteCommand commands_[kCommandQueueSize];
    size_t commandCount_ = 0;
};
