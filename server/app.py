"""Backend del control remoto del Semaforo Woffu.

Un unico proceso aiohttp que:

- acepta la conexion WebSocket saliente del dispositivo (``/ws/device``,
  autenticada con HTTP Basic: nombre del dispositivo + password de su AP);
- sirve la web app (``/``) protegida con login (mismo usuario/password);
- hace de puente entre ambos (``/ws/ui``): reenvia a los navegadores el
  estado, eventos y log del dispositivo, y al dispositivo los comandos.

Todo el estado vive en memoria (ultimo estado + historial reciente de eventos
y log): se pierde al reiniciar el contenedor, y el dispositivo lo vuelve a
mandar al reconectar. Protocolo completo en Arquitectura.md, ``## Control
remoto``.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import hmac
import json
import logging
import os
import secrets
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

from aiohttp import WSMsgType, web

log = logging.getLogger("semaforo")

STATIC_DIR = Path(__file__).parent / "static"
SESSION_COOKIE = "semaforo_session"
SESSION_TTL_S = 30 * 24 * 3600
EVENT_HISTORY = 200
LOG_HISTORY = 1500
ACK_TIMEOUT_S = 90  # ota_update puede tardar (comprobacion contra GitHub)
LOGIN_MAX_FAILURES = 5
LOGIN_WINDOW_S = 15 * 60

# Comandos que la web app puede mandar al dispositivo (el resto se rechaza
# aqui mismo, sin llegar al dispositivo).
ALLOWED_COMMANDS = {"sign_in", "sign_out", "refresh", "led_set", "led_auto", "reboot", "ota_update", "status"}


def parse_devices(spec: str) -> dict[str, str]:
    """``"Semaforo-A1B2C3:12345678,Semaforo-...:..."`` -> {nombre: password}."""
    devices = {}
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        name, sep, password = item.partition(":")
        if not sep or not name or not password:
            raise SystemExit(f"DEVICES mal formado: {item!r} (esperado Nombre:password)")
        devices[name.strip()] = password.strip()
    return devices


@dataclass
class PendingCommand:
    cmd: str
    sent_at: float
    rid: str  # id de peticion de la web app, para casar el ack con su boton


@dataclass
class DeviceHub:
    """Estado en memoria de un dispositivo y sus navegadores conectados."""

    name: str
    device_ws: web.WebSocketResponse | None = None
    hello: dict | None = None
    status: dict | None = None
    status_at: float | None = None
    connected_at: float | None = None
    last_seen: float | None = None
    events: deque = field(default_factory=lambda: deque(maxlen=EVENT_HISTORY))
    logs: deque = field(default_factory=lambda: deque(maxlen=LOG_HISTORY))
    uis: set = field(default_factory=set)
    pending: dict = field(default_factory=dict)

    @property
    def online(self) -> bool:
        return self.device_ws is not None and not self.device_ws.closed

    def presence(self) -> dict:
        return {
            "type": "presence",
            "online": self.online,
            "connected_at": self.connected_at,
            "last_seen": self.last_seen,
            "hello": self.hello,
        }

    def snapshot(self) -> dict:
        return {
            "type": "snapshot",
            "name": self.name,
            "server_time": time.time(),
            "presence": self.presence(),
            "status": self.status,
            "status_at": self.status_at,
            "events": list(self.events),
            "logs": list(self.logs),
        }

    async def broadcast(self, message: dict) -> None:
        data = json.dumps(message)
        for ws in list(self.uis):
            try:
                await ws.send_str(data)
            except Exception:  # navegador ya cerrado: se limpia en su handler
                self.uis.discard(ws)

    async def add_event(self, kind: str, msg: str, source: str = "device") -> None:
        event = {"t": time.time(), "kind": kind, "msg": msg, "source": source}
        self.events.append(event)
        await self.broadcast({"type": "event", "event": event})


class LoginLimiter:
    """Bloquea una IP tras LOGIN_MAX_FAILURES fallos en LOGIN_WINDOW_S."""

    def __init__(self) -> None:
        self._failures: dict[str, deque] = {}

    def _recent(self, ip: str) -> deque:
        q = self._failures.setdefault(ip, deque())
        cutoff = time.time() - LOGIN_WINDOW_S
        while q and q[0] < cutoff:
            q.popleft()
        return q

    def blocked(self, ip: str) -> bool:
        return len(self._recent(ip)) >= LOGIN_MAX_FAILURES

    def fail(self, ip: str) -> None:
        self._recent(ip).append(time.time())

    def success(self, ip: str) -> None:
        self._failures.pop(ip, None)


class App:
    def __init__(self, devices: dict[str, str], secret: bytes) -> None:
        self.devices = devices
        self.secret = secret
        self.hubs = {name: DeviceHub(name) for name in devices}
        self.limiter = LoginLimiter()

    # --- autenticacion -------------------------------------------------

    def check_credentials(self, name: str, password: str) -> bool:
        expected = self.devices.get(name)
        # compare_digest incluso si el nombre no existe, para no filtrar por
        # tiempo de respuesta que nombres son validos.
        return hmac.compare_digest((expected or "\0").encode(), password.encode()) and expected is not None

    def _session_sig(self, name: str, expires: int) -> str:
        # La password entra en la firma: si cambia (otro dispositivo con el
        # mismo nombre, DEVICES editado), las sesiones viejas dejan de valer.
        payload = f"{name}|{expires}|{self.devices.get(name, '')}".encode()
        return hmac.new(self.secret, payload, hashlib.sha256).hexdigest()

    def make_session(self, name: str) -> str:
        expires = int(time.time()) + SESSION_TTL_S
        return f"{name}|{expires}|{self._session_sig(name, expires)}"

    def session_device(self, request: web.Request) -> str | None:
        raw = request.cookies.get(SESSION_COOKIE)
        if not raw:
            return None
        try:
            name, expires_s, sig = raw.rsplit("|", 2)
            expires = int(expires_s)
        except ValueError:
            return None
        if name not in self.devices or expires < time.time():
            return None
        if not hmac.compare_digest(sig, self._session_sig(name, expires)):
            return None
        return name

    @staticmethod
    def client_ip(request: web.Request) -> str:
        # nginx (repo infra) anade la IP real al final de X-Forwarded-For; lo
        # que venga antes lo controla el cliente, no es fiable.
        forwarded = request.headers.get("X-Forwarded-For")
        if forwarded:
            return forwarded.split(",")[-1].strip()
        return request.remote or "?"

    @staticmethod
    def same_origin(request: web.Request) -> bool:
        origin = request.headers.get("Origin")
        if origin is None:
            return True
        host = request.headers.get("X-Forwarded-Host") or request.host
        return origin in (f"https://{host}", f"http://{host}")

    # --- paginas ---------------------------------------------------------

    async def index(self, request: web.Request) -> web.StreamResponse:
        if self.session_device(request) is None:
            raise web.HTTPFound("/login")
        return web.FileResponse(STATIC_DIR / "index.html", headers={"Cache-Control": "no-cache"})

    async def login_page(self, request: web.Request, error: str = "", status: int = 200) -> web.Response:
        html = (STATIC_DIR / "login.html").read_text()
        html = html.replace("%ERROR%", f'<p class="error">{error}</p>' if error else "")
        return web.Response(text=html, content_type="text/html", status=status)

    async def login(self, request: web.Request) -> web.StreamResponse:
        ip = self.client_ip(request)
        if self.limiter.blocked(ip):
            log.warning("login bloqueado para %s", ip)
            return await self.login_page(request, "Demasiados intentos. Espera unos minutos.", 429)
        form = await request.post()
        name = str(form.get("username", "")).strip()
        password = str(form.get("password", ""))
        if not self.check_credentials(name, password):
            self.limiter.fail(ip)
            log.warning("login fallido de %s para %r", ip, name)
            await asyncio.sleep(1)
            return await self.login_page(request, "Usuario o password incorrectos.", 401)
        self.limiter.success(ip)
        log.info("login correcto de %s para %s", ip, name)
        response = web.HTTPFound("/")
        response.set_cookie(
            SESSION_COOKIE,
            self.make_session(name),
            max_age=SESSION_TTL_S,
            httponly=True,
            secure=request.headers.get("X-Forwarded-Proto", request.scheme) == "https",
            samesite="Strict",
        )
        raise response

    async def logout(self, request: web.Request) -> web.StreamResponse:
        response = web.HTTPFound("/login")
        response.del_cookie(SESSION_COOKIE)
        raise response

    async def healthz(self, request: web.Request) -> web.Response:
        return web.json_response({name: hub.online for name, hub in self.hubs.items()})

    # --- WebSocket del dispositivo --------------------------------------

    def _basic_auth(self, request: web.Request) -> tuple[str, str] | None:
        header = request.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            return None
        try:
            decoded = base64.b64decode(header[6:]).decode()
        except Exception:
            return None
        name, sep, password = decoded.partition(":")
        return (name, password) if sep else None

    async def device_ws(self, request: web.Request) -> web.StreamResponse:
        ip = self.client_ip(request)
        if self.limiter.blocked(ip):
            raise web.HTTPTooManyRequests()
        creds = self._basic_auth(request)
        if creds is None or not self.check_credentials(*creds):
            self.limiter.fail(ip)
            log.warning("dispositivo rechazado desde %s (%s)", ip, creds[0] if creds else "sin credenciales")
            raise web.HTTPUnauthorized(headers={"WWW-Authenticate": 'Basic realm="semaforo"'})
        name = creds[0]
        hub = self.hubs[name]

        # El ESP32 manda ping cada 25s; heartbeat aqui detecta ademas conexiones
        # muertas sin cierre limpio (corte de WiFi en la oficina).
        ws = web.WebSocketResponse(heartbeat=40, max_msg_size=64 * 1024)
        await ws.prepare(request)

        if hub.online:
            log.info("%s: nueva conexion, se cierra la anterior", name)
            await hub.device_ws.close(code=4000, message=b"replaced")
        hub.device_ws = ws
        hub.connected_at = hub.last_seen = time.time()
        log.info("%s: conectado desde %s", name, ip)
        await hub.broadcast(hub.presence())
        await hub.add_event("presence", "Dispositivo conectado", "server")

        try:
            async for msg in ws:
                if msg.type != WSMsgType.TEXT:
                    continue
                hub.last_seen = time.time()
                try:
                    data = json.loads(msg.data)
                except ValueError:
                    log.warning("%s: mensaje no JSON", name)
                    continue
                await self.handle_device_message(hub, data)
        finally:
            if hub.device_ws is ws:
                hub.device_ws = None
                hub.last_seen = time.time()
                log.info("%s: desconectado", name)
                await hub.broadcast(hub.presence())
                await hub.add_event("presence", "Dispositivo desconectado", "server")
                for cmd_id, pending in list(hub.pending.items()):
                    await self.finish_command(hub, cmd_id, False, "el dispositivo se desconecto")
        return ws

    async def handle_device_message(self, hub: DeviceHub, data: dict) -> None:
        kind = data.get("type")
        if kind == "hello":
            hub.hello = {k: data.get(k) for k in ("name", "fw", "reset_reason")}
            await hub.broadcast(hub.presence())
            await hub.add_event(
                "presence", f"Firmware {data.get('fw')} (ultimo reinicio: {data.get('reset_reason')})", "device"
            )
        elif kind == "status":
            data.pop("type", None)
            hub.status = data
            hub.status_at = time.time()
            await hub.broadcast({"type": "status", "status": data, "status_at": hub.status_at})
        elif kind == "event":
            await hub.add_event(str(data.get("kind", "info")), str(data.get("msg", "")))
        elif kind == "log":
            lines = []
            for entry in data.get("lines", []):
                if isinstance(entry, list) and len(entry) == 2:
                    item = {"t": time.time(), "seq": entry[0], "line": str(entry[1])}
                    hub.logs.append(item)
                    lines.append(item)
            if lines:
                await hub.broadcast({"type": "log", "lines": lines})
        elif kind == "ack":
            await self.finish_command(hub, str(data.get("id", "")), bool(data.get("ok")), str(data.get("msg", "")))

    async def finish_command(self, hub: DeviceHub, cmd_id: str, ok: bool, msg: str) -> None:
        pending = hub.pending.pop(cmd_id, None)
        if pending is None:
            return
        await hub.broadcast({"type": "ack", "id": cmd_id, "rid": pending.rid, "cmd": pending.cmd, "ok": ok, "msg": msg})
        if not ok:
            await hub.add_event("error", f"{pending.cmd}: {msg}", "server")

    async def expire_commands(self) -> None:
        while True:
            await asyncio.sleep(5)
            now = time.time()
            for hub in self.hubs.values():
                for cmd_id, pending in list(hub.pending.items()):
                    if now - pending.sent_at > ACK_TIMEOUT_S:
                        await self.finish_command(hub, cmd_id, False, "sin respuesta del dispositivo")

    # --- WebSocket de la web app -----------------------------------------

    async def ui_ws(self, request: web.Request) -> web.StreamResponse:
        name = self.session_device(request)
        if name is None:
            raise web.HTTPUnauthorized()
        if not self.same_origin(request):
            raise web.HTTPForbidden()
        hub = self.hubs[name]
        ws = web.WebSocketResponse(heartbeat=30, max_msg_size=16 * 1024)
        await ws.prepare(request)
        hub.uis.add(ws)
        try:
            await ws.send_str(json.dumps(hub.snapshot()))
            async for msg in ws:
                if msg.type != WSMsgType.TEXT:
                    continue
                try:
                    data = json.loads(msg.data)
                except ValueError:
                    continue
                if data.get("type") == "cmd":
                    await self.send_command(hub, ws, data)
        finally:
            hub.uis.discard(ws)
        return ws

    async def send_command(self, hub: DeviceHub, origin: web.WebSocketResponse, data: dict) -> None:
        cmd = str(data.get("cmd", ""))
        rid = str(data.get("rid", ""))[:16]
        cmd_id = secrets.token_hex(4)

        async def reject(msg: str) -> None:
            await origin.send_str(json.dumps({"type": "ack", "id": cmd_id, "rid": rid, "cmd": cmd, "ok": False,
                                              "msg": msg}))

        if cmd not in ALLOWED_COMMANDS:
            await reject("comando no permitido")
            return
        if not hub.online:
            await reject("el dispositivo no esta conectado")
            return
        message = {"type": "cmd", "id": cmd_id, "cmd": cmd}
        if cmd == "led_set":
            message["color"] = str(data.get("color", ""))
            message["mode"] = str(data.get("mode", "solid"))
        hub.pending[cmd_id] = PendingCommand(cmd, time.time(), rid)
        await hub.device_ws.send_str(json.dumps(message))
        log.info("%s: comando %s (%s)", hub.name, cmd, cmd_id)


def build_app() -> web.Application:
    devices = parse_devices(os.environ.get("DEVICES", ""))
    if not devices:
        raise SystemExit("Falta DEVICES (p.ej. DEVICES=Semaforo-A1B2C3:12345678), ver server/README.md")
    secret_env = os.environ.get("SESSION_SECRET")
    # Sin SESSION_SECRET fijo, las sesiones caducan en cada reinicio del contenedor.
    secret = secret_env.encode() if secret_env else secrets.token_bytes(32)

    state = App(devices, secret)
    app = web.Application()
    app.add_routes([
        web.get("/", state.index),
        web.get("/login", state.login_page),
        web.post("/login", state.login),
        web.post("/logout", state.logout),
        web.get("/healthz", state.healthz),
        web.get("/ws/device", state.device_ws),
        web.get("/ws/ui", state.ui_ws),
        web.static("/static", STATIC_DIR),
    ])

    async def start_background(app: web.Application) -> None:
        app["expire_task"] = asyncio.create_task(state.expire_commands())

    async def stop_background(app: web.Application) -> None:
        app["expire_task"].cancel()

    async def close_sockets(app: web.Application) -> None:
        for hub in state.hubs.values():
            for ws in list(hub.uis) + ([hub.device_ws] if hub.device_ws else []):
                await ws.close(code=1001, message=b"server shutdown")

    app.on_startup.append(start_background)
    app.on_cleanup.append(stop_background)
    app.on_shutdown.append(close_sockets)
    log.info("dispositivos configurados: %s", ", ".join(devices))
    return app


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    web.run_app(build_app(), host=os.environ.get("HOST", "0.0.0.0"), port=int(os.environ.get("PORT", "8095")),
                access_log=None)
