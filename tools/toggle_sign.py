#!/usr/bin/env python3
"""Verifica el flujo de fichar/defichar (toggle) contra la API de Woffu.

AVISO: a diferencia de check_status.py (solo lectura), este script tiene un
efecto real sobre la cuenta: si el usuario esta fichado lo desficha, y
viceversa. Pide confirmacion interactiva antes de hacer el POST, salvo que se
pase --yes.
"""

import argparse
import getpass
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

LOGIN_URL = "https://app.woffu.com/api/svc/accounts/authorization/token"
USERS_URL = "https://app.woffu.com/api/users"
SLOTS_URL = "https://app.woffu.com/api/svc/signs/v2/signs/slots"
SIGNS_URL = "https://app.woffu.com/api/svc/signs/signs"


def login(username: str, password: str) -> str:
    data = urllib.parse.urlencode({
        "grant_type": "password",
        "username": username,
        "password": password,
    }).encode()
    req = urllib.request.Request(
        LOGIN_URL,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        body = json.load(resp)
    return body["accessToken"]


def get_user_id(token: str) -> str:
    req = urllib.request.Request(
        USERS_URL,
        headers={"Authorization": f"Bearer {token}"},
        method="GET",
    )
    with urllib.request.urlopen(req) as resp:
        body = json.load(resp)
    return body["UserId"]


def get_slots(token: str) -> list:
    req = urllib.request.Request(
        SLOTS_URL,
        headers={"Authorization": f"Bearer {token}"},
        method="GET",
    )
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


def compute_status(slots: list) -> str:
    if not slots:
        return "NO_FICHADO"
    last = slots[-1]
    if last.get("in") and not last.get("out"):
        return "FICHADO"
    return "NO_FICHADO"


def local_timezone_offset_minutes() -> int:
    # Misma convencion que JS Date.prototype.getTimezoneOffset(): minutos al
    # oeste de UTC, positivo si la zona esta detras de UTC. time.timezone/
    # time.altzone de Python ya usan ese mismo signo (a diferencia de
    # datetime.now().astimezone().utcoffset(), que va invertido).
    is_dst = time.localtime().tm_isdst > 0
    offset_seconds = time.altzone if is_dst else time.timezone
    return offset_seconds // 60


def toggle_sign(token: str, user_id) -> tuple:
    body = json.dumps({
        "agreementEventId": None,
        "requestId": None,
        "deviceId": "WebApp",
        "latitude": None,
        "longitude": None,
        "timezoneOffset": local_timezone_offset_minutes()
    }).encode()
    req = urllib.request.Request(
        SIGNS_URL,
        data=body,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, resp.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", help="si se omite, se pide de forma interactiva")
    parser.add_argument("-v", "--verbose", action="store_true", help="muestra las respuestas completas")
    parser.add_argument("--yes", action="store_true", help="no pedir confirmacion antes de fichar/defichar")
    args = parser.parse_args()

    password = args.password or getpass.getpass("Password: ")

    try:
        token = login(args.username, password)
        if args.verbose:
            print(f"accessToken: {token[:20]}...", file=sys.stderr)

        user_id = get_user_id(token)
        if args.verbose:
            print(f"UserId: {user_id}", file=sys.stderr)

        slots_before = get_slots(token)
        status_before = compute_status(slots_before)
        if args.verbose:
            print("Slots (antes):", json.dumps(slots_before, indent=2), file=sys.stderr)
        print(f"Estado antes: {status_before}")

        if not args.yes:
            accion = "DESFICHAR" if status_before == "FICHADO" else "FICHAR"
            resp = input(f"Esto va a {accion} de verdad en tu cuenta real de Woffu. Continuar? [s/N]: ")
            if resp.strip().lower() not in ("s", "si", "y", "yes"):
                print("Cancelado.", file=sys.stderr)
                return 1

        status_code, body = toggle_sign(token, user_id)
        print(f"POST {SIGNS_URL} -> {status_code}")
        if args.verbose and body:
            print("Respuesta:", body, file=sys.stderr)

        slots_after = get_slots(token)
        status_after = compute_status(slots_after)
        if args.verbose:
            print("Slots (despues):", json.dumps(slots_after, indent=2), file=sys.stderr)
        print(f"Estado despues: {status_after}")

    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} en {e.url}: {e.read().decode(errors='replace')}", file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Error de red: {e.reason}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
