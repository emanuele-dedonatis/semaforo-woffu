# Tools

Scripts auxiliares de desarrollo, no forman parte del firmware.

## `dev_version.py`

Hook de PlatformIO (`extra_scripts` en `platformio.ini`), no se ejecuta a mano. Embebe `FIRMWARE_VERSION` en builds locales a partir de git (`<último tag con patch+1>-dev.<hash corto>`); se salta si el build ya trae la versión fijada por `PLATFORMIO_BUILD_FLAGS` (caso de `tools/build_firmware.sh` en release). Ver `## OTA — detalle de implementación` en [Arquitectura.md](../Arquitectura.md).

## `check_status.py`

Reproduce el flujo de login + consulta de estado de fichaje contra la API de Woffu (ver [Arquitectura.md](../Arquitectura.md#cliente-woffu)), para verificar el comportamiento de la API antes/durante la implementación del firmware.

Requiere Python 3, sin dependencias externas.

```
python3 tools/check_status.py --username tu@email.com
```

- Si no se pasa `--password`, se pide de forma interactiva (no queda en el historial de la shell).
- `-v` / `--verbose` muestra por stderr el JSON completo de login y de slots.
- Por stdout imprime únicamente `FICHADO` o `NO_FICHADO`.

## `balance.py`

Responde a "¿cómo voy de horas ahora mismo?": muestra el saldo (trabajado − teórico) de la **semana**, el **mes** y el **año** en curso, **contando el fichaje abierto de hoy**.

Solo lectura, no ficha ni desficha. Requiere Python 3.9+, sin dependencias externas.

```
python3 tools/balance.py --username tu@email.com
```

```
Hoy 2026-08-14 a las 15:45 (Europe/Madrid)
  FICHADO desde las 08:32 - trabajado hoy 7h 12m de 7h 30m teóricas

Periodo  Desde           Teórico  Trabajado      Saldo   Saldo positivo a las
Semana   2026-08-10      37h 30m    36h 49m    -0h 40m                  16:25
Mes      2026-08-01      75h 00m    71h 01m    -3h 58m                  19:43
Año      2026-01-01    1200h 00m  1205h 51m    +5h 51m                     ya
```

Por qué no vale con leer la web: la columna "Diferencia" de *Mi Presencia* sale de

```
GET /api/svc/core/diariesquery/users/{userId}/diaries/summary/presence
    ?userId={userId}&fromDate=YYYY-MM-DD&toDate=YYYY-MM-DD
    &pageSize=N&includeNotHourTypes=true&includeDifference=true
```

y ese endpoint **no cuenta el tramo abierto**: mientras estás fichado, hoy computa como jornada entera en negativo (`differenceTime: -27000` = −7h30m). El script consulta cada rango (`totalDifferenceTime` viene en segundos, `totalScheduleTimeFormatted` en horas/minutos), calcula aparte lo trabajado hoy sumando los slots de `/signs/v2/signs/slots` (el tramo abierto hasta "ahora") y **sustituye** el día de hoy en cada total. Si hoy no hay ningún slot, se respeta lo que diga Woffu.

- "Saldo positivo a las" = hora a la que hay que desfichar hoy para dejar ese periodo a cero (`-` si no estás fichado, `ya` si el saldo ya es positivo).
- `--tz` (por defecto `Europe/Madrid`): Woffu devuelve las horas en la zona de la empresa, así que ejecutarlo en un contenedor en UTC daría un "ahora" desfasado.
- `--host`: el endpoint de diarios se pide a `app.woffu.com`; si tu tenant no lo sirve ahí, se prueba con el subdominio de la empresa que devuelve `/api/companies`, y con `--host <empresa>.woffu.com` se fuerza a mano.
- `-v` / `--verbose` muestra por stderr el UserId, los slots y el resumen de hoy.

## `toggle_sign.py`

Verifica el flujo de **fichar/defichar** (`POST /api/svc/signs/signs`) contra la API real de Woffu, antes/durante la implementación de `WoffuClient::toggleSign()` en el firmware (fichaje por NFC, ver `Arquitectura.md`).

**AVISO**: a diferencia de `check_status.py` (solo lectura), este script **tiene un efecto real** sobre la cuenta: si estás fichado te desficha, y viceversa (el propio backend de Woffu decide la dirección, no hay parámetro para elegirla). Pide confirmación interactiva antes del POST, salvo que se pase `--yes`.

Requiere Python 3, sin dependencias externas.

```
python3 tools/toggle_sign.py --username tu@email.com
```

- Muestra el estado antes y después del toggle (reutilizando la misma lógica que `check_status.py`).
- `-v` / `--verbose` muestra por stderr el UserId, el cuerpo de la respuesta del POST y los slots completos.
- `--yes` salta la confirmación interactiva (útil para pruebas repetidas).
