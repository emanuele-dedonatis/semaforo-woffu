# Arquitectura

Basado en [Requisitos.md](Requisitos.md). Documento vivo, igual que el de requisitos: lo que no esté marcado como pregunta abierta es una decisión de diseño tomada.

## Máquina de estados de alto nivel

```
        ┌──────┐
        │ INIT │  (carga config de NVS)
        └──┬───┘
           │
   ¿configured == false?
     │              │
    sí              no
     │              │
     ▼              ▼
┌───────────┐  ┌─────────────┐
│UNCONFIGURED│  │ BLE_WINDOW  │  (60s, o hasta desconexión si hay pairing)
│(BLE siempre│  │ (BLE activo,│
│  activo)   │  │ WiFi ya     │
└─────┬──────┘  │ conectando  │
      │         │ en paralelo)│
      │         └──────┬──────┘
      │ config          │ timeout sin pairing,
      │ válida          │ o disconnect tras pairing
      │ recibida        │
      ▼                 ▼
 [guarda NVS,      ┌──────────┐
  reboot]  ───────▶│ RUNNING  │  (BLE apagado, scheduler + polling Woffu)
                    └──────────┘
```

- **INIT**: lee `Config` de NVS (Preferences), decide rama según `configured`.
- **UNCONFIGURED**: BLE anunciando indefinidamente. WiFi apagado (no hay credenciales aún).
- **BLE_WINDOW**: solo quien ya tiene `configured == true`. WiFi se intenta conectar en paralelo (coexistencia WiFi+BLE del ESP32, comparten radio 2.4GHz pero el controller gestiona el time-slicing; para JSON pequeños y polling poco frecuente no es un problema). Si nadie empareja en 60s → `RUNNING`. Si empareja, la ventana quedaría abierta, pero como se ve abajo, en la práctica cualquier escritura de config válida dispara un reboot inmediato.
- **RUNNING**: BLE apagado del todo (para no dejar superficie de ataque innecesaria mientras opera). Corre el scheduler de polling contra Woffu y refleja el estado en el semáforo.

Cualquier escritura de configuración válida provoca un **reboot automático inmediato** (tras confirmar el ACK por BLE), en vez de esperar a la desconexión del móvil. Evita tener que tirar el stack BLE y levantar WiFi en caliente dentro del mismo proceso. Si el usuario quiere seguir cambiando cosas, vuelve a entrar en la ventana de 60s tras el reboot.

## Tareas FreeRTOS y comunicación

No hace falta una arquitectura muy fragmentada; con 3 flujos de ejecución es suficiente:

| Tarea | Quién la crea | Responsabilidad |
|---|---|---|
| `loopTask` (la del framework Arduino) | Arduino core | Orquestador: máquina de estados, scheduler de polling, llamadas HTTP a Woffu, disparo de OTA |
| `LedTask` | `xTaskCreate` en `setup()` | Animación de LEDs (PWM/blink) independiente, para que una llamada HTTP lenta no congele el parpadeo |
| Tarea interna de NimBLE | La librería NimBLE-Arduino | Stack BLE, callbacks de conexión/escritura |

Comunicación entre tareas por **colas FreeRTOS** (evita mutex explícitos donde no hace falta):

- `ledCmdQueue` (orquestador → LedTask): `{color, mode: OFF|SOLID|BLINK_SLOW|BLINK_FAST, brightness}`
- `bleEventQueue` (callbacks BLE → orquestador): `CONFIG_RECEIVED`, `OTA_REQUESTED`, `CLIENT_CONNECTED`, `CLIENT_DISCONNECTED`
- El struct `Config` en RAM se escribe solo desde el callback BLE y se lee solo desde el orquestador tras un evento `CONFIG_RECEIVED` — sin necesidad de mutex porque no hay lectura/escritura concurrente real (todo pasa por la cola de eventos).

## Estructura del proyecto (PlatformIO)

```
platformio.ini
partitions.csv
src/
  main.cpp                 # setup()/loop(), arranque de tareas
  app/
    AppStateMachine.{h,cpp}   # estados de arriba, orquestación
    Scheduler.{h,cpp}         # ventanas activa/pasiva/off → intervalo de poll
  config/
    Config.{h,cpp}            # struct + wrapper sobre Preferences (NVS)
  net/
    WifiManager.{h,cpp}
    TimeSync.{h,cpp}          # NTP + zona horaria (setenv TZ + configTzTime)
    WoffuClient.{h,cpp}       # llamadas a la API de Woffu
    OtaUpdater.{h,cpp}        # HTTPUpdate contra GitHub Releases
  ble/
    BleProvisioning.{h,cpp}   # servidor NimBLE, GATT, callbacks
  led/
    LedController.{h,cpp}     # LedTask, LEDC PWM, patrones de parpadeo
```

## Partition table

Se necesitan dos particiones OTA (`app0`/`app1`) + `otadata`. No hace falta SPIFFS/LittleFS (toda la config vive en NVS, no hay assets web que servir). Usar el esquema `default_ota.csv` que trae PlatformIO/arduino-esp32 de serie (dos slots de ~1.9MB cada uno, sobra para este firmware), o una tabla propia mínima si se quiere afinar tamaños.

## Esquema de configuración (NVS / Preferences)

Namespace `cfg`:

| Key | Tipo | Ejemplo |
|---|---|---|
| `configured` | bool | `true` |
| `wifi_ssid` | string | |
| `wifi_pass` | string | |
| `woffu_domain` | string | |
| `woffu_user` | string | |
| `woffu_pass` | string | |
| `tz` | string (POSIX TZ) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| `win_in` | `{start,end}` (minutos del día) | `450,540` (07:30–09:00) |
| `win_out` | `{start,end}` | `840,1080` (14:00–18:00) |
| `poll_active_s` | uint16 | `45` |
| `poll_passive_s` | uint16 | `900` |
| `brightness` | uint8 (0-255) | `180` |

**Sin cifrado de NVS para el MVP**: cifrar de verdad el contenido de NVS en el ESP32 requiere activar *flash encryption* a nivel de eFuse (paso de fábrica/primer flasheo, irreversible en modo *release*, y que complica el flujo de OTA con firmas/cifrado de imágenes). Queda descartado para el MVP; la config permanece protegida solo por el pairing BLE con PIN. Se anota como posible mejora futura opcional para quien quiera más hardening.

## Diseño BLE GATT

Un único servicio custom (UUID propio generado para el proyecto), con 3 características para que sea manejable a mano desde nRF Connect:

| Característica | Propiedades | Contenido |
|---|---|---|
| `CONFIG` | Write | JSON con todos los campos de la tabla de arriba (excepto `configured`, que lo pone el firmware al validar) |
| `STATUS` | Read, Notify | JSON: `{"fw":"1.2.0","configured":true,"wifi":"connected","ip":"...","result":"ok"}` — feedback de éxito/error tras escribir `CONFIG` o pedir OTA |
| `COMMAND` | Write | String simple: `"OTA_CHECK"`, `"FACTORY_RESET"` |

Un solo blob JSON en `CONFIG` en vez de una característica por campo: para el usuario es una única escritura de texto (nRF Connect permite escribir strings UTF-8), y en firmware es un único parseo con ArduinoJson en vez de gestionar el estado parcial de múltiples características.

`FACTORY_RESET` en `COMMAND`: borra la config de NVS (`configured = false`) y reinicia — vuelve directo a `UNCONFIGURED`.

## Cliente Woffu

Basado en las llamadas capturadas (ver `curl.md`, no versionado — contiene credenciales reales).

### Login

```
POST https://app.woffu.com/api/svc/accounts/authorization/token
Content-Type: application/x-www-form-urlencoded

grant_type=password&username=<user>&password=<pass>
```

Nótese que el login va siempre contra el host fijo `app.woffu.com`, **no** contra el dominio/subdominio de empresa — el propio JWT devuelto ya lleva embebido el `CompanyId`. El campo `woffu_domain` de la config solo hace falta para la siguiente llamada.

Respuesta:
```json
{
  "accessToken": "<JWT>",
  "tokenType": "bearer",
  "expiresIn": 31536000,
  "requiresTwoFactor": false,
  "twoFactorToken": null
}
```

`expiresIn` es de **~1 año**, así que no hace falta reautenticar en cada poll: se hace login una vez al arrancar `RUNNING` (o al primer poll), se guarda el `accessToken` **solo en RAM** (no en NVS, para no persistir un token de vida larga en flash sin cifrar), y se reutiliza en todos los polls siguientes hasta que una llamada devuelva 401 — en ese caso se repite el login una vez y se reintenta la llamada. Esto no contradice la política de "sin reintentos" de Requisitos.md: esa regla habla de fallos de red/disponibilidad, aquí es un refresco de credenciales esperado, acotado a un único reintento.

### Estado de fichaje

```
GET https://<woffu_domain>/api/svc/signs/v2/signs/slots
Authorization: Bearer <accessToken>
```

Devuelve un array de "slots" del día actual (el servidor decide qué es "hoy"; no se manda fecha explícita — confirmado que tras medianoche el array pasa a vacío):

```json
[
  {
    "in":  { "shortTrueTime": "08:18:22", "signType": 0, "signEventId": "..." },
    "out": { "shortTrueTime": "14:12:37", "signType": 0, "signEventId": "..." },
    "motive": { "name": null }
  }
]
```

Lógica de interpretación (puede haber varios slots en el día, p.ej. con pausas — el último es siempre el más reciente):

- Array vacío → **no fichado** (rojo) — todavía no hay eventos hoy.
- Último elemento tiene `in` pero **no** `out` → **fichado** (verde) — la última acción fue una entrada sin salida posterior.
- Último elemento tiene `in` **y** `out` → **no fichado** (rojo) — la última acción fue una salida.

Cualquier otro caso (respuesta inesperada, error HTTP, timeout, JSON inválido) → ámbar, sin reintento, como ya definido en Requisitos.md.

### `platformio.ini` / TLS

Ambos hosts (`app.woffu.com` y `<woffu_domain>`) por HTTPS — usar el certificate bundle de `arduino-esp32` (igual que para OTA) en vez de pinnear certificados.

## OTA — detalle de implementación

- URL estable de GitHub Releases: `https://github.com/emanuele-dedonatis/semaforo-woffu/releases/latest/download/firmware.bin` — siempre apunta al asset de la última release, sin necesidad de llamar a la API de GitHub ni parsear JSON.
- Antes de descargar el `.bin` (varios cientos de KB/pocos MB), descargar primero un asset pequeño `version.txt` de la misma release y compararlo con la versión actual (embebida en el firmware) — si coincide, no hace falta bajar el binario entero.
- Validación TLS: usar el **certificate bundle** que trae `arduino-esp32` (vía `WiFiClientSecure` + bundle de CAs de Mozilla) en vez de pinnear certificados a mano — así no se rompe si GitHub o Woffu rotan certificados.
- Tras un flasheo OTA correcto, `HTTPUpdate` deja marcado el nuevo slot como boot y reinicia solo.

## LED Controller

- 3 canales LEDC (PWM) en los GPIOs de R/Y/G, activo-alto (cátodo común, confirmado).
- `LedTask` interpreta `{color, mode, brightness}` de la cola: `SOLID` (duty fijo), `BLINK_SLOW` (ciclo ~1000ms), `BLINK_FAST` (~250ms), `OFF` (duty 0).
- Brillo (`brightness` de la config) se aplica como escala del duty cycle cuando el LED está en fase "encendido".

## CI/CD — release y publicación del firmware

Repo: [emanuele-dedonatis/semaforo-woffu](https://github.com/emanuele-dedonatis/semaforo-woffu).

El versionado y la publicación de releases se automatizan con **semantic-release**, apoyándose en los Conventional Commits (ver `CLAUDE.md`), en un workflow de GitHub Actions disparado en push a `main`:

1. `semantic-release` analiza los commits desde el último release. Si no hay `feat`/`fix`/breaking changes (p.ej. solo `docs`/`chore`), no genera release nuevo — no hay build de firmware de por medio.
2. Si toca versión nueva, antes de publicar (plugin `exec`, hook `prepareCmd`) se lanza `pio run` pasando la versión calculada como build flag (para que el firmware conozca su propia versión en runtime) y se genera `version.txt` con ese número.
3. El plugin `@semantic-release/github` publica el Release con `firmware.bin` y `version.txt` como assets adjuntos automáticamente, dejándolos disponibles en `.../releases/latest/download/...` sin pasos manuales.

El workflow concreto (`.github/workflows/release.yml`) se escribe cuando exista el esqueleto de PlatformIO (necesita el nombre del entorno/board definido en `platformio.ini`).

## Estado

Diseño cerrado, sin preguntas abiertas pendientes.
