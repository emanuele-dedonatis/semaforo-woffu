"""Simulador del dispositivo para probar el backend y la web app sin ESP32.

Habla el mismo protocolo que firmware/src/net/RemoteLink.cpp (ver
Arquitectura.md, `## Control remoto`): se autentica con nombre + token (el
servidor lo registra la primera vez), manda hello (con la password del
dispositivo), status periodico, log y eventos, y responde con ack a cada
comando, con un estado de Woffu/LED/configuracion inventado.

Uso:
    python3 tools/fake_device.py --url ws://127.0.0.1:8095/ws/device \\
        --name Semaforo-A1B2C3 --token 0123456789abcdef0123456789abcdef
"""

import argparse
import asyncio
import base64
import json
import random
import time

import aiohttp


class FakeDevice:
    def __init__(self, password: str) -> None:
        self.woffu = "out"
        self.override = None
        self.seq = 0
        self.boot = time.time()
        self.password = password
        self.config = {
            "wifi_ssid": "Oficina", "woffu_user": "yo@empresa.com",
            "window_start": "07:30", "window_end": "19:00", "auto_sign": False,
            "auto_sign_schedule": [{"start": "08:30", "end": "16:30"}] * 4 + [{"start": "08:30", "end": "14:00"}],
            "remote_url": "wss://woffu.ededona.es/ws/device", "ota_auto": False,
        }
        self.card = True

    def led(self) -> dict:
        if self.override:
            return self.override
        return {"color": {"in": "green", "out": "red"}.get(self.woffu, "yellow"), "mode": "solid"}

    def status(self) -> dict:
        auto = {"color": {"in": "green", "out": "red"}.get(self.woffu, "yellow"), "mode": "solid"}
        return {
            "type": "status", "fw": "9.9.9-fake", "uptime_s": int(time.time() - self.boot), "epoch": int(time.time()),
            "wifi": {"ssid": self.config["wifi_ssid"], "rssi": random.randint(-70, -50), "ip": "192.168.1.50"},
            "heap": {"free": 120000, "min_free": 90000, "max_alloc": 60000},
            "woffu": {"status": self.woffu, "credentials_invalid": False, "last_poll_epoch": int(time.time())},
            "mode": "active", "mode_reason": "dentro de la ventana de encendido configurada",
            "workday": {"valid": True, "weekend": False, "holiday": False},
            "window": {"start": self.config["window_start"], "end": self.config["window_end"]},
            "led": self.led(), "led_auto": auto, "override": self.override,
            "nfc": {"reader": True, "card": self.card},
            "auto_sign": {"enabled": self.config["auto_sign"], "today_start": "08:30", "today_end": "16:30"},
            "config": {**self.config, "has_password": bool(self.password)},
            "ota": {"latest": "9.9.9-fake", "available": False, "last_check_epoch": int(self.boot)},
        }

    def log(self, text: str) -> dict:
        self.seq += 1
        return {"type": "log", "lines": [[self.seq, time.strftime("[%H:%M:%S] ") + text]]}

    def handle(self, cmd: dict) -> list[dict]:
        name = cmd.get("cmd")
        out = [self.log(f"Control remoto: comando '{name}' recibido.")]
        ok, msg, after = True, "ok", []
        if name in ("sign_in", "sign_out"):
            want = "in" if name == "sign_in" else "out"
            if self.woffu == want:
                msg = "ya estaba fichada, no se hace nada."
            else:
                self.woffu = want
                msg = "fichaje registrado."
                out.append({"type": "event", "kind": "sign", "msg": f"Fichaje remoto: {msg}"})
        elif name == "refresh":
            msg = f"Estado Woffu: {self.woffu}"
        elif name == "led_set":
            self.override = {"color": cmd.get("color"), "mode": cmd.get("mode") or "solid"}
            if self.override["color"] == "off":
                self.override["mode"] = "off"
            msg = "color fijo aplicado"
            out.append({"type": "event", "kind": "led", "msg": f"Color fijo: {self.override['color']}"})
        elif name == "led_auto":
            self.override = None
            msg = "modo automatico"
        elif name == "config_set":
            changes = dict(cmd.get("config") or {})
            for secret in ("wifi_pass", "woffu_pass"):
                changes.pop(secret, None)
            pw = changes.pop("device_password", None)
            if pw is not None and pw and not 8 <= len(pw) <= 63:
                ok, msg = False, "No se ha guardado: la password debe tener entre 8 y 63 caracteres."
            else:
                self.config.update({k: v for k, v in changes.items() if k in self.config})
                msg = "Guardado."
                if pw is not None and pw != self.password:
                    self.password = pw
                    msg = "Guardado. Password cambiada: vuelve a entrar."
                    after.append({"type": "password", "password": pw})
        elif name in ("ota_check", "ota_update"):
            msg = "Ya esta en la ultima version (9.9.9-fake)."
        elif name == "nfc_learn":
            msg = "Acerca la tarjeta al lector (30 s)."
            after.append({"type": "event", "kind": "nfc", "msg": "Tarjeta NFC aprendida."})
        elif name in ("reboot", "factory_reset"):
            msg = "reiniciando (simulado)"
        elif name != "status":
            ok, msg = False, f"comando desconocido: {name}"
        out.append({"type": "ack", "id": cmd.get("id"), "ok": ok, "msg": msg})
        out.extend(after)
        out.append(self.status())
        return out


async def run(url: str, name: str, token: str, password: str) -> None:
    dev = FakeDevice(password)
    basic = base64.b64encode(f"{name}:{token}".encode()).decode()
    headers = {"Authorization": f"Basic {basic}"}
    while True:
        try:
            async with aiohttp.ClientSession() as session:
                async with session.ws_connect(url, headers=headers, heartbeat=25) as ws:
                    print("conectado")
                    await ws.send_json({"type": "hello", "name": name, "fw": "9.9.9-fake",
                                        "reset_reason": "power-on", "password": dev.password})
                    await ws.send_json(dev.log("Control remoto: conectado al servidor."))
                    await ws.send_json(dev.status())

                    async def periodic() -> None:
                        while True:
                            await asyncio.sleep(10)
                            await ws.send_json(dev.log(f"Estado Woffu: {dev.woffu}"))
                            await ws.send_json(dev.status())

                    task = asyncio.create_task(periodic())
                    try:
                        async for msg in ws:
                            data = json.loads(msg.data)
                            if data.get("type") == "cmd":
                                await asyncio.sleep(0.5)  # latencia de la llamada a Woffu
                                for out in dev.handle(data):
                                    await ws.send_json(out)
                    finally:
                        task.cancel()
        except aiohttp.WSServerHandshakeError as e:
            print(f"rechazado: {e.status}")
        except aiohttp.ClientError as e:
            print(f"error: {e}")
        print("desconectado, reintentando en 3s")
        await asyncio.sleep(3)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default="ws://127.0.0.1:8095/ws/device")
    parser.add_argument("--name", default="Semaforo-A1B2C3")
    parser.add_argument("--token", default="0123456789abcdef0123456789abcdef")
    parser.add_argument("--password", default="", help="password del dispositivo (vacia = sin password)")
    args = parser.parse_args()
    asyncio.run(run(args.url, args.name, args.token, args.password))
