"""Simulador del dispositivo para probar el backend y la web app sin ESP32.

Habla el mismo protocolo que firmware/src/net/RemoteLink.cpp (ver
Arquitectura.md, `## Control remoto`): hello, status periodico, log, eventos
y ack de cada comando, con un estado de Woffu/LED inventado.

Uso:
    python3 tools/fake_device.py --url ws://127.0.0.1:8095/ws/device \
        --name Semaforo-A1B2C3 --password 12345678
"""

import argparse
import asyncio
import base64
import json
import random
import time

import aiohttp


class FakeDevice:
    def __init__(self) -> None:
        self.woffu = "out"
        self.override = None
        self.seq = 0
        self.boot = time.time()

    def led(self) -> dict:
        if self.override:
            return self.override
        return {"color": {"in": "green", "out": "red"}.get(self.woffu, "yellow"), "mode": "solid"}

    def status(self) -> dict:
        auto = {"color": {"in": "green", "out": "red"}.get(self.woffu, "yellow"), "mode": "solid"}
        return {
            "type": "status", "fw": "9.9.9-fake", "uptime_s": int(time.time() - self.boot), "epoch": int(time.time()),
            "wifi": {"ssid": "Oficina", "rssi": random.randint(-70, -50), "ip": "192.168.1.50"},
            "heap": {"free": 120000, "min_free": 90000, "max_alloc": 60000},
            "woffu": {"status": self.woffu, "credentials_invalid": False, "last_poll_epoch": int(time.time())},
            "mode": "active", "mode_reason": "dentro de la ventana de encendido configurada",
            "workday": {"valid": True, "weekend": False, "holiday": False},
            "window": {"start": "07:30", "end": "19:00"},
            "led": self.led(), "led_auto": auto, "override": self.override,
            "nfc": {"reader": True, "card": True},
            "auto_sign": {"enabled": False, "today_start": "08:30", "today_end": "16:30"},
        }

    def log(self, text: str) -> dict:
        self.seq += 1
        return {"type": "log", "lines": [[self.seq, time.strftime("[%H:%M:%S] ") + text]]}

    def handle(self, cmd: dict) -> list[dict]:
        name = cmd.get("cmd")
        out = [self.log(f"Control remoto: comando '{name}' recibido.")]
        ok, msg = True, "ok"
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
        elif name == "ota_update":
            msg = "Ya esta en la ultima version (9.9.9-fake)."
        elif name == "reboot":
            msg = "reiniciando (simulado)"
        elif name != "status":
            ok, msg = False, f"comando desconocido: {name}"
        out.append({"type": "ack", "id": cmd.get("id"), "ok": ok, "msg": msg})
        out.append(self.status())
        return out


async def run(url: str, name: str, password: str) -> None:
    dev = FakeDevice()
    token = base64.b64encode(f"{name}:{password}".encode()).decode()
    headers = {"Authorization": f"Basic {token}"}
    while True:
        try:
            async with aiohttp.ClientSession() as session:
                async with session.ws_connect(url, headers=headers, heartbeat=25) as ws:
                    print("conectado")
                    await ws.send_json({"type": "hello", "name": name, "fw": "9.9.9-fake", "reset_reason": "power-on"})
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
        except aiohttp.ClientResponseError as e:
            print(f"rechazado: {e.status}")
        except aiohttp.ClientError as e:
            print(f"error: {e}")
        print("desconectado, reintentando en 3s")
        await asyncio.sleep(3)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default="ws://127.0.0.1:8095/ws/device")
    parser.add_argument("--name", default="Semaforo-A1B2C3")
    parser.add_argument("--password", default="12345678")
    args = parser.parse_args()
    asyncio.run(run(args.url, args.name, args.password))
