#!/usr/bin/env python3
"""Saldo de horas (trabajado - teórico) contra la API de Woffu, contando el día de hoy en curso.

La columna "Diferencia" de la web (endpoint diaries/summary/presence) no cuenta
el fichaje abierto de hoy: mientras estás fichado, hoy cuenta como -jornada
completa. Este script recalcula el día de hoy a partir de /signs/slots (sumando
el tramo abierto hasta "ahora") y lo sustituye en los totales de la semana, el
mes y el año en curso.

Solo lectura: no ficha ni desficha nada.
"""

import argparse
import getpass
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta
from zoneinfo import ZoneInfo

LOGIN_URL = "https://app.woffu.com/api/svc/accounts/authorization/token"
USERS_URL = "https://app.woffu.com/api/users"
SLOTS_URL = "https://app.woffu.com/api/svc/signs/v2/signs/slots"
COMPANIES_URL = "https://app.woffu.com/api/companies"

# El host generico app.woffu.com sirve para login/slots/users; el endpoint de
# diarios puede requerir el subdominio del tenant (ver --host en el README).
DEFAULT_DIARIES_HOST = "app.woffu.com"

# Woffu devuelve las horas en la zona horaria de la empresa, no en UTC: si el
# script corre en un contenedor en UTC, "ahora" se iría varias horas. Se fija
# por defecto la zona de la empresa (España peninsular) y se puede cambiar.
DEFAULT_TZ = "Europe/Madrid"


def _get_json(url: str, token: str):
    req = urllib.request.Request(
        url,
        headers={"Authorization": f"Bearer {token}", "Accept": "application/json"},
        method="GET",
    )
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


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
        return json.load(resp)["accessToken"]


def get_user_id(token: str) -> str:
    return _get_json(USERS_URL, token)["UserId"]


def get_slots(token: str) -> list:
    return _get_json(SLOTS_URL, token)


def discover_hosts(token: str) -> list:
    """Subdominios del tenant, por si app.woffu.com no sirve el endpoint de diarios."""
    try:
        body = _get_json(COMPANIES_URL, token)
    except (urllib.error.HTTPError, urllib.error.URLError, ValueError):
        return []
    entries = body if isinstance(body, list) else [body]
    hosts = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        for key in ("Domain", "domain", "CompanyDomain", "Url", "url"):
            value = entry.get(key)
            if isinstance(value, str) and value.strip():
                host = value.strip().replace("https://", "").replace("http://", "").strip("/")
                if "." not in host:
                    host = f"{host}.woffu.com"
                if host not in hosts:
                    hosts.append(host)
    return hosts


def diaries_summary(host: str, token: str, user_id, from_date: date, to_date: date) -> dict:
    days = (to_date - from_date).days + 1
    query = urllib.parse.urlencode({
        "userId": user_id,
        "fromDate": from_date.isoformat(),
        "toDate": to_date.isoformat(),
        "pageSize": days + 1,
        "includeNotHourTypes": "true",
        "includeDifference": "true",
    })
    url = (f"https://{host}/api/svc/core/diariesquery/users/{user_id}"
           f"/diaries/summary/presence?{query}")
    return _get_json(url, token)


def parse_formatted(fmt) -> int:
    """Convierte un *Formatted de Woffu ({"values": ["-7", "30"]}) a segundos."""
    if not isinstance(fmt, dict):
        return 0
    values = fmt.get("values") or []
    if not values:
        return 0
    head = str(values[0]).strip()
    sign = -1 if head.startswith("-") else 1
    hours = int(head.lstrip("+-") or 0)
    minutes = int(str(values[1]).lstrip("+-") or 0) if len(values) > 1 else 0
    return sign * (hours * 3600 + minutes * 60)


def slot_seconds(endpoint) -> int | None:
    """Segundos desde medianoche del lado `in`/`out` de un slot, o None si no existe."""
    if not endpoint:
        return None
    if isinstance(endpoint, str):
        text = endpoint
    elif isinstance(endpoint, dict):
        text = next((endpoint[k] for k in ("shortTrueTime", "shortTime", "trueTime", "time", "date")
                     if isinstance(endpoint.get(k), str)), None)
        if text is None:
            return None
    else:
        return None
    if "T" in text:
        text = text.split("T", 1)[1]
    parts = text.split("+")[0].split("Z")[0].split(":")
    try:
        h, m = int(parts[0]), int(parts[1])
        s = int(float(parts[2])) if len(parts) > 2 else 0
    except (ValueError, IndexError):
        return None
    return h * 3600 + m * 60 + s


def worked_today(slots: list, now_seconds: int) -> tuple[int, int | None]:
    """(segundos trabajados hoy, hora de entrada del tramo abierto o None)."""
    total = 0
    open_since = None
    for slot in slots or []:
        start = slot_seconds(slot.get("in"))
        if start is None:
            continue
        end = slot_seconds(slot.get("out"))
        if end is None:
            open_since = start
            end = now_seconds
        total += max(0, end - start)
    return total, open_since


def fmt_hm(seconds: int, signed: bool = True) -> str:
    sign = "-" if seconds < 0 else ("+" if signed else "")
    seconds = abs(int(seconds))
    return f"{sign}{seconds // 3600}h {seconds % 3600 // 60:02d}m"


def fmt_clock(seconds: int) -> str:
    seconds = int(seconds) % 86400
    return f"{seconds // 3600:02d}:{seconds % 3600 // 60:02d}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", help="si se omite, se pide de forma interactiva")
    parser.add_argument("--host", help=f"host del endpoint de diarios (por defecto {DEFAULT_DIARIES_HOST} "
                                       "y, si falla, el subdominio de la empresa)")
    parser.add_argument("--tz", default=DEFAULT_TZ, help=f"zona horaria de la empresa (por defecto {DEFAULT_TZ})")
    parser.add_argument("-v", "--verbose", action="store_true", help="muestra las respuestas completas por stderr")
    args = parser.parse_args()

    password = args.password or getpass.getpass("Password: ")
    now = datetime.now(ZoneInfo(args.tz))
    today = now.date()
    now_seconds = now.hour * 3600 + now.minute * 60 + now.second

    try:
        token = login(args.username, password)
        user_id = get_user_id(token)
        if args.verbose:
            print(f"UserId: {user_id}", file=sys.stderr)

        slots = get_slots(token)
        if args.verbose:
            print("Slots de hoy:", json.dumps(slots, indent=2), file=sys.stderr)

        hosts = [args.host] if args.host else [DEFAULT_DIARIES_HOST]
        periods = [
            ("Semana", today - timedelta(days=today.weekday())),
            ("Mes", today.replace(day=1)),
            ("Año", today.replace(month=1, day=1)),
        ]

        # El resumen de hoy sale de su propia consulta (1 día) para no depender
        # de la paginación de los rangos largos.
        summaries = {}
        last_error = None
        for host in hosts + ([] if args.host else discover_hosts(token)):
            try:
                summaries["Hoy"] = diaries_summary(host, token, user_id, today, today)
            except urllib.error.HTTPError as e:
                last_error = e
                continue
            for name, start in periods:
                summaries[name] = diaries_summary(host, token, user_id, start, today)
            break
        else:
            raise last_error

    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} en {e.url}: {e.read().decode(errors='replace')}", file=sys.stderr)
        if e.code in (403, 404):
            print("Prueba con --host <empresa>.woffu.com", file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Error de red: {e.reason}", file=sys.stderr)
        return 1

    if args.verbose:
        print("Resumen de hoy:", json.dumps(summaries["Hoy"], indent=2), file=sys.stderr)

    today_schedule = parse_formatted(summaries["Hoy"].get("totalScheduleTimeFormatted"))
    today_reported_diff = summaries["Hoy"].get("totalDifferenceTime") or 0

    today_worked, open_since = worked_today(slots, now_seconds)
    today_diff = today_worked - today_schedule
    # Si no hay slots (día sin fichajes todavía) se respeta lo que diga Woffu.
    correction = (today_diff - today_reported_diff) if slots else 0

    print(f"Hoy {today.isoformat()} a las {fmt_clock(now_seconds)} ({args.tz})")
    if open_since is not None:
        print(f"  FICHADO desde las {fmt_clock(open_since)} - trabajado hoy {fmt_hm(today_worked, signed=False)} "
              f"de {fmt_hm(today_schedule, signed=False)} teóricas")
    else:
        print(f"  NO FICHADO - trabajado hoy {fmt_hm(today_worked, signed=False)} "
              f"de {fmt_hm(today_schedule, signed=False)} teóricas")

    print()
    print(f"{'Periodo':<8} {'Desde':<12} {'Teórico':>10} {'Trabajado':>10} {'Saldo':>10}   {'Saldo positivo a las':>20}")
    for name, start in periods:
        summary = summaries[name]
        theoretical = parse_formatted(summary.get("totalScheduleTimeFormatted"))
        balance = (summary.get("totalDifferenceTime") or 0) + correction
        worked = theoretical + balance
        if balance >= 0:
            break_even = "ya"
        elif open_since is not None:
            break_even = fmt_clock(now_seconds - balance)
        else:
            break_even = "-"
        print(f"{name:<8} {start.isoformat():<12} {fmt_hm(theoretical, signed=False):>10} "
              f"{fmt_hm(worked, signed=False):>10} {fmt_hm(balance):>10}   {break_even:>20}")

    print()
    print("Saldo positivo a las = hora a la que hay que desfichar hoy para dejar ese periodo a cero.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
