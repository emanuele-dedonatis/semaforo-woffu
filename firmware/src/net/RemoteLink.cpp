#include "RemoteLink.h"

#include <WebSocketsClient.h>

#include "Log.h"
#include "Version.h"
#include "net/CertBundle.h"

namespace {
constexpr uint32_t kStatusPeriodMs = 60000;      // estado completo periodico aunque no cambie nada
constexpr uint32_t kHeartbeatPingMs = 25000;     // ping WS (mantiene viva la conexion a traves de nginx/NAT)
constexpr uint32_t kHeartbeatPongTimeoutMs = 10000;
constexpr uint8_t kHeartbeatMissesToDrop = 2;
// Reintentos de conexion espaciados segun cuanto tiempo llevamos sin
// conectar: cada intento puede bloquear loopTask durante el handshake TLS
// (o hasta el timeout TCP si el servidor no responde), asi que no conviene
// insistir cada pocos segundos si el servidor esta caido de verdad.
constexpr uint32_t kReconnectFastMs = 5000;      // primer minuto
constexpr uint32_t kReconnectMediumMs = 30000;   // hasta 10 min
constexpr uint32_t kReconnectSlowMs = 120000;    // a partir de ahi
constexpr size_t kMaxLogLinesPerMessage = 16;

const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_EXT: return "external";
        default: return "unknown";
    }
}

// Separa "wss://host[:puerto]/ruta" en sus partes. Devuelve false si no es
// una URL ws:// o wss:// valida.
bool parseUrl(const String& url, bool& secure, String& host, uint16_t& port, String& path) {
    String rest;
    if (url.startsWith("wss://")) {
        secure = true;
        port = 443;
        rest = url.substring(6);
    } else if (url.startsWith("ws://")) {
        secure = false;
        port = 80;
        rest = url.substring(5);
    } else {
        return false;
    }
    int slash = rest.indexOf('/');
    String hostPort = slash >= 0 ? rest.substring(0, slash) : rest;
    path = slash >= 0 ? rest.substring(slash) : "/";
    int colon = hostPort.indexOf(':');
    if (colon >= 0) {
        host = hostPort.substring(0, colon);
        long p = hostPort.substring(colon + 1).toInt();
        if (p <= 0 || p > 65535) {
            return false;
        }
        port = static_cast<uint16_t>(p);
    } else {
        host = hostPort;
    }
    return !host.isEmpty();
}
}  // namespace

void RemoteLink::begin(const String& url, const String& user, const String& password) {
    enabled_ = false;
    if (url.isEmpty()) {
        logPrintln("Control remoto: deshabilitado (sin URL de servidor configurada).");
        return;
    }
    if (!parseUrl(url, secure_, host_, port_, path_)) {
        logPrintf("Control remoto: URL de servidor invalida (%s), deshabilitado.\n", url.c_str());
        return;
    }
    user_ = user;
    password_ = password;
    enabled_ = true;
    logPrintf("Control remoto: servidor %s://%s:%u%s, dispositivo %s.\n", secure_ ? "wss" : "ws", host_.c_str(),
              port_, path_.c_str(), user_.c_str());
}

void RemoteLink::loop() {
    if (!enabled_) {
        return;
    }
    if (!started_) {
        // Se arranca la primera vez que hay WiFi+hora (no en begin()), para
        // que el primer intento de handshake TLS no falle por reloj a 1970.
        started_ = true;
        ws_ = new WebSocketsClient();
        if (secure_) {
            ws_->beginSslWithBundle(host_.c_str(), port_, path_.c_str(), certBundle(), "");
        } else {
            ws_->begin(host_.c_str(), port_, path_.c_str(), "");
        }
        ws_->setAuthorization(user_.c_str(), password_.c_str());
        ws_->onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
            onEvent(static_cast<int>(type), payload, length);
        });
        ws_->enableHeartbeat(kHeartbeatPingMs, kHeartbeatPongTimeoutMs, kHeartbeatMissesToDrop);
        currentReconnectMs_ = kReconnectFastMs;
        ws_->setReconnectInterval(currentReconnectMs_);
        disconnectedSinceMs_ = millis();
        logPrintln("Control remoto: conectando al servidor...");
    }

    if (!connected_) {
        uint32_t downFor = millis() - disconnectedSinceMs_;
        uint32_t wanted = downFor < 60000 ? kReconnectFastMs
                          : downFor < 600000 ? kReconnectMediumMs
                                             : kReconnectSlowMs;
        if (wanted != currentReconnectMs_) {
            currentReconnectMs_ = wanted;
            ws_->setReconnectInterval(wanted);
        }
    }

    ws_->loop();

    if (connected_) {
        flushLogs();
    }
}

bool RemoteLink::connected() const {
    return enabled_ && connected_;
}

void RemoteLink::onEvent(int type, uint8_t* payload, size_t length) {
    switch (static_cast<WStype_t>(type)) {
        case WStype_CONNECTED: {
            connected_ = true;
            logPrintln("Control remoto: conectado al servidor.");
            JsonDocument hello;
            hello["type"] = "hello";
            hello["name"] = user_;
            hello["fw"] = FIRMWARE_VERSION;
            hello["reset_reason"] = resetReasonName();
            sendJson(hello);
            // Al conectar se manda todo lo que quede del historial de log que
            // no se haya mandado ya (en el primer arranque, las lineas de
            // boot/portal), y un estado completo en cuanto el orquestador
            // pase por takeStatusDue().
            statusRequested_ = true;
            break;
        }
        case WStype_DISCONNECTED:
            if (connected_) {
                connected_ = false;
                disconnectedSinceMs_ = millis();
                logPrintln("Control remoto: desconectado del servidor, reintentando...");
            }
            break;
        case WStype_TEXT:
            handleText(reinterpret_cast<const char*>(payload), length);
            break;
        default:
            break;
    }
}

void RemoteLink::handleText(const char* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
        logPrintln("Control remoto: mensaje no valido del servidor (JSON).");
        return;
    }
    const char* type = doc["type"] | "";
    if (strcmp(type, "cmd") != 0) {
        return;
    }
    RemoteCommand cmd;
    cmd.id = doc["id"] | "";
    cmd.cmd = doc["cmd"] | "";
    cmd.color = doc["color"] | "";
    cmd.mode = doc["mode"] | "";
    if (commandCount_ >= kCommandQueueSize) {
        sendAck(cmd.id, false, "dispositivo ocupado, demasiados comandos en cola");
        return;
    }
    commands_[commandCount_++] = cmd;
}

bool RemoteLink::takeCommand(RemoteCommand& out) {
    if (commandCount_ == 0) {
        return false;
    }
    out = commands_[0];
    for (size_t i = 1; i < commandCount_; i++) {
        commands_[i - 1] = commands_[i];
    }
    commandCount_--;
    return true;
}

bool RemoteLink::takeStatusDue() {
    if (!connected_) {
        return false;
    }
    if (statusRequested_ || millis() - lastStatusMs_ >= kStatusPeriodMs) {
        statusRequested_ = false;
        lastStatusMs_ = millis();
        return true;
    }
    return false;
}

void RemoteLink::sendStatus(JsonDocument& doc) {
    doc["type"] = "status";
    sendJson(doc);
}

void RemoteLink::sendAck(const String& id, bool ok, const String& message) {
    JsonDocument doc;
    doc["type"] = "ack";
    doc["id"] = id;
    doc["ok"] = ok;
    doc["msg"] = message;
    sendJson(doc);
}

void RemoteLink::sendEvent(const char* kind, const String& message) {
    JsonDocument doc;
    doc["type"] = "event";
    doc["kind"] = kind;
    doc["msg"] = message;
    sendJson(doc);
}

bool RemoteLink::sendJson(JsonDocument& doc) {
    if (!connected_ || ws_ == nullptr) {
        return false;
    }
    String out;
    serializeJson(doc, out);
    return ws_->sendTXT(out);
}

void RemoteLink::flushLogs() {
    if (logLastSeq() == lastLogSeqSent_) {
        return;
    }
    // Se acumulan en un unico mensaje (hasta kMaxLogLinesPerMessage lineas)
    // en vez de un mensaje por linea. Las lineas que genere el propio envio
    // (p.ej. un aviso de desconexion) se mandan en la siguiente vuelta.
    struct Ctx {
        JsonArray lines;
        uint32_t lastSeq;
        size_t count;
    };
    JsonDocument doc;
    doc["type"] = "log";
    Ctx ctx{doc["lines"].to<JsonArray>(), lastLogSeqSent_, 0};
    logForEachSince(
        lastLogSeqSent_,
        [](uint32_t seq, const char* line, void* p) {
            Ctx* c = static_cast<Ctx*>(p);
            if (c->count >= kMaxLogLinesPerMessage) {
                return;
            }
            JsonArray entry = c->lines.add<JsonArray>();
            entry.add(seq);
            entry.add(line);
            c->lastSeq = seq;
            c->count++;
        },
        &ctx);
    if (ctx.count == 0) {
        lastLogSeqSent_ = logLastSeq();
        return;
    }
    // Si el envio falla, lastLogSeqSent_ no avanza y se reintenta en la
    // proxima vuelta desde el mismo punto (si las lineas siguen en el historial).
    if (sendJson(doc)) {
        lastLogSeqSent_ = ctx.lastSeq;
    }
}
