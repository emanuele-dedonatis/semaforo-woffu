#!/usr/bin/env python3
"""Verifica el flujo de login + estado de fichaje contra la API de Woffu."""

import argparse
import getpass
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

LOGIN_URL = "https://app.woffu.com/api/svc/accounts/authorization/token"


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


SLOTS_URL = "https://app.woffu.com/api/svc/signs/v2/signs/slots"


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", help="si se omite, se pide de forma interactiva")
    parser.add_argument("-v", "--verbose", action="store_true", help="muestra las respuestas completas")
    args = parser.parse_args()

    password = args.password or getpass.getpass("Password: ")

    try:
        token = login(args.username, password)
        if args.verbose:
            print(f"accessToken: {token[:20]}...", file=sys.stderr)

        slots = get_slots(token)
        if args.verbose:
            print(json.dumps(slots, indent=2), file=sys.stderr)

    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} en {e.url}: {e.read().decode(errors='replace')}", file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Error de red: {e.reason}", file=sys.stderr)
        return 1

    print(compute_status(slots))
    return 0


if __name__ == "__main__":
    sys.exit(main())
