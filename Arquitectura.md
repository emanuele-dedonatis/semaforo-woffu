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
┌───────────┐  ┌───────────────┐
│UNCONFIGURED│  │ PORTAL_WINDOW │  (10s iniciales sin nadie conectado;
│(AP siempre │  │ (solo AP, sin │   sin límite mientras haya alguien
│  activo)   │  │  STA todavía) │   conectado; cierra al desconectar)
└─────┬──────┘  └──────┬────────┘
      │ formulario       │ timeout sin nadie conectado,
      │ guardado         │ o se desconecta el último cliente
      ▼                  ▼
 [guarda NVS,       ┌──────────┐
  reboot]  ────────▶│ RUNNING  │  (AP apagado, arranca WiFi STA,
                     └──────────┘   scheduler + polling Woffu)
```

- **INIT**: lee `Config` de NVS (Preferences), decide rama según `configured`.
- **UNCONFIGURED**: `WiFi.softAP` anunciando indefinidamente (modo `WIFI_AP`, sin STA — no hay credenciales aún).
- **PORTAL_WINDOW**: solo quien ya tiene `configured == true`. Aquí el ESP32 mantiene el AP del portal **y** conecta a la WiFi real ya guardada en paralelo (`WIFI_MODE_APSTA`, vía `WifiManager::begin()` combinando el bit STA con el AP activo) — necesario para que el botón de OTA del portal (disponible en esta ventana, ver `## OTA`) tenga salida a internet sin depender de cerrar antes el portal. Espera 10s a que alguien se conecte al AP; si nadie lo hace, pasa a `RUNNING`. Si alguien se conecta, la ventana se queda abierta sin límite de tiempo mientras siga conectado (`WiFi.softAPgetStationNum() > 0`, consultado cada `loop()`), y se cierra en el momento en que se desconecta — sin depender de ningún timeout adicional. Al entrar en `RUNNING`, `enterRunning()` solo repite `wifi_.begin()` si la STA no quedó ya conectada desde aquí.
- **RUNNING**: AP y servidor web apagados del todo (para no dejar superficie de ataque innecesaria mientras opera). Arranca la conexión WiFi STA a la red real. Corre el scheduler de polling contra Woffu y refleja el estado en el semáforo.

Al guardar el formulario, el dispositivo persiste en NVS y hace **reboot automático inmediato** (tras responder la página de confirmación HTTP). Si el usuario quiere seguir cambiando cosas, vuelve a entrar en la ventana de portal tras el reboot.

## Tareas y comunicación

Aquí sí que la arquitectura es más simple que con BLE: `WebServer`/`DNSServer` son **síncronos** y se bombean explícitamente desde `loop()` (no crean su propia tarea FreeRTOS como hacía el stack de NimBLE), así que todo corre en la tarea `loopTask` del framework Arduino, salvo el parpadeo de LEDs:

| Tarea | Quién la crea | Responsabilidad |
|---|---|---|
| `loopTask` (la del framework Arduino) | Arduino core | Orquestador: máquina de estados, bombeo de `ProvisioningPortal` (DNS+HTTP), scheduler de polling, llamadas HTTP a Woffu, disparo de OTA |
| `LedTask` | `xTaskCreate` en `setup()` | Animación de LEDs (PWM/blink) independiente, para que una llamada HTTP lenta no congele el parpadeo |

Comunicación:

- `ledCmdQueue` (orquestador → LedTask): `{color, mode: OFF|SOLID|BLINK_SLOW|BLINK_FAST, brightness}` — sigue siendo una cola FreeRTOS porque cruza tareas de verdad.
- Orquestador ↔ `ProvisioningPortal`: **sin cola** — como los handlers HTTP se ejecutan síncronamente dentro de la misma llamada a `portal_.loop()` (propia tarea `loopTask`), basta con banderas/valores miembro normales (`takeConfigToSave()`, `takeOtaRequested()`, `takeFactoryResetRequested()`), sin necesidad de mecanismos cross-task.

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
    CertBundle.{h,cpp}        # adjunta el bundle de CAs embebido a un WiFiClientSecure
  web/
    ProvisioningPortal.{h,cpp} # AP WiFi + DNS captivo + servidor HTTP con el formulario
  led/
    LedController.{h,cpp}     # LedTask, LEDC PWM, patrones de parpadeo
data/
  cert/x509_crt_bundle.bin  # bundle de CAs de Mozilla, vendorizado (ver sección TLS)
```

## Partition table

Se necesitan dos particiones OTA (`app0`/`app1`) + `otadata`. No hace falta SPIFFS/LittleFS (toda la config vive en NVS; la página HTML del portal va embebida como constante en el propio firmware, no como asset servido desde un filesystem). Se usa el esquema `min_spiffs.csv` que trae `arduino-esp32` de serie: dos slots OTA de ~1,9MB cada uno. Tras quitar NimBLE (ver decisión más abajo) el firmware bajó a ~42% de un slot, con margen de sobra incluso para el esquema `default.csv` más ajustado (1,25MB) — se mantiene `min_spiffs.csv` de todas formas, sin urgencia por volver atrás.

## Esquema de configuración (NVS / Preferences)

Namespace `cfg`:

| Key | Tipo | Ejemplo |
|---|---|---|
| `configured` | bool | `true` |
| `wifi_ssid` | string | |
| `wifi_pass` | string | |
| `woffu_user` | string | |
| `woffu_pass` | string | |
| `tz` | string (POSIX TZ) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| `win_in` | `{start,end}` (minutos del día) | `450,540` (07:30–09:00) |
| `win_out` | `{start,end}` | `840,1080` (14:00–18:00) |
| `poll_active_s` | uint16 | `45` |
| `poll_passive_s` | uint16 | `900` |
| `brightness` | uint8 (0-255) | `180` |
| `force_active` | bool | `false` |

**Sin cifrado de NVS para el MVP**: cifrar de verdad el contenido de NVS en el ESP32 requiere activar *flash encryption* a nivel de eFuse (paso de fábrica/primer flasheo, irreversible en modo *release*, y que complica el flujo de OTA con firmas/cifrado de imágenes). Queda descartado para el MVP; la config permanece protegida solo por la password del AP de configuración. Se anota como posible mejora futura opcional para quien quiera más hardening.

## Portal de configuración (WiFi AP + HTTP)

**Decisión revisada**: el diseño original usaba BLE (NimBLE) con un servicio GATT custom. Se descartó tras probarlo en hardware real por ser poco amigable para usuarios no técnicos que van a compartir el dispositivo: apps genéricas como nRF Connect muestran "Unknown Characteristic" para UUIDs custom (no leen el descriptor de nombre para la cabecera), y hay que escribir cada característica una por una entendiendo su formato. Se sustituye por un **portal cautivo WiFi**, el patrón estándar en IoT doméstico (enchufes inteligentes, Chromecast, etc.): sin apps, funciona igual en Android que en iPhone (a diferencia de una alternativa evaluada con Web Bluetooth, que no funciona en Safari/iOS).

### Flujo

1. El dispositivo crea su propia red WiFi (`WiFi.softAP`), SSID `Semaforo-XXXXXX` (últimos 3 bytes de la MAC, igual que antes), protegida con WPA2 y una password de 8 dígitos derivada de la MAC (mismo principio que el PIN de BLE, pero con longitud válida para WPA2-PSK, que exige mínimo 8 caracteres).
2. Un `DNSServer` interno resuelve **todas** las consultas DNS a la IP propia del AP (`192.168.4.1`), y el `WebServer` redirige (302) cualquier ruta no reconocida a `/`. Combinados, esto dispara la detección automática de "portal cautivo" de Android/iOS/Windows en la mayoría de los casos (se abre solo un navegador con la página de configuración al conectar a la red). Si el sistema operativo no lo detecta automáticamente, `192.168.4.1` siempre funciona navegando a mano.
3. `GET /` sirve un formulario HTML normal (controles nativos: texto, password, `<input type="time">` para las horas — con selector nativo en móvil, `<input type="number">` para los campos numéricos), prellenado con la configuración actual guardada. La página muestra también la versión de firmware actual (`FIRMWARE_VERSION`, ver `## OTA — detalle de implementación`), útil para confirmar visualmente que un OTA se aplicó. Por el mismo motivo, también se imprime por Serial al arrancar (`main.cpp`).
4. `POST /save` guarda todos los campos del formulario a la vez (a diferencia del diseño BLE por característica individual, aquí no hace falta un paso `APPLY_CONFIG` separado — el propio HTML envía todos los campos juntos en cada submit), responde una página de confirmación, y el orquestador persiste en NVS + reinicia.
5. `POST /ota` y `POST /factory-reset`: botones sueltos en la misma página que disparan esas acciones (banderas leídas por el orquestador, igual que el guardado).

### `ProvisioningPortal` (`src/web/ProvisioningPortal.{h,cpp}`)

- Envuelve `WebServer` (puerto 80) + `DNSServer` (puerto 53), ambos bundled en `arduino-esp32`, sin dependencias externas nuevas.
- `hasClient()` expone `WiFi.softAPgetStationNum() > 0` — usado por el orquestador tanto para el patrón de LED (`ALL SOLID` mientras haya alguien conectado, `BLINK_SLOW` si no) como para decidir cuándo cerrar `PORTAL_WINDOW` (no se cierra por timeout mientras haya alguien conectado; se cierra en el momento en que se desconecta el último cliente).
- `takeConfigToSave()` / `takeOtaRequested()` / `takeFactoryResetRequested()`: banderas de "un solo uso" (se limpian al leerlas) que el orquestador consulta cada `loop()` — sin colas, ver sección de tareas.
- La plantilla HTML va embebida como constante `R"HTML(...)"` en el propio `.cpp`; los valores actuales se insertan con `String::replace()` sobre placeholders `%CAMPO%`, con un `htmlEscape()` mínimo (`&`, `"`, `<`, `>`) para no romper el HTML si algún valor guardado contiene esos caracteres.

## Cliente Woffu

Basado en llamadas capturadas manualmente desde el navegador y verificadas con `tools/check_status.py`.

### Login

```
POST https://app.woffu.com/api/svc/accounts/authorization/token
Content-Type: application/x-www-form-urlencoded

grant_type=password&username=<user>&password=<pass>
```

El login va contra el host fijo `app.woffu.com`, el propio JWT devuelto ya lleva embebido el `CompanyId`. Se ha verificado (`tools/check_status.py`) que ese mismo host fijo también sirve para la llamada de estado de fichaje (ver más abajo), así que **no hace falta pedir el dominio/subdominio de empresa** en el provisioning — un campo menos que rellenar en el portal.

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
GET https://app.woffu.com/api/svc/signs/v2/signs/slots
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

### TLS

`app.woffu.com` por HTTPS — usar el certificate bundle de `arduino-esp32` (igual que para OTA) en vez de pinnear certificados. Ver detalle de cómo se genera y embebe en `## Certificate bundle (TLS)`.

## Certificate bundle (TLS)

`WoffuClient` y `OtaUpdater` comparten el mismo mecanismo de validación TLS: el **certificate bundle** de `arduino-esp32` (bundle de CAs de Mozilla, vía `WiFiClientSecure::setCACertBundle()`), en vez de pinnear certificados a mano — así no se rompe si GitHub o Woffu rotan certificados.

- El bundle **no viene pre-generado** en el core `arduino-esp32`: hay que construirlo con `gen_crt_bundle.py` (utilidad de ESP-IDF) a partir de una lista de CAs en PEM.
- **Decisión**: se genera una vez y se **vendoriza** en el repo como binario (`data/cert/x509_crt_bundle.bin`, ~54KB), en vez de regenerarlo en cada build. Prioriza build simple y reproducible (sin dependencia de red ni del paquete Python `cryptography` en cada máquina/CI) a cambio de tener que regenerarlo a mano de vez en cuando si caducan CAs. Encaja con el margen de flash ya previsto para esto (ver `## Partition table`).
- Generado con: `gen_crt_bundle.py` de la versión de ESP-IDF que trae el core instalado (`tools/sdk/versions.txt` del paquete `framework-arduinoespressif32`, actualmente IDF v4.4.7) + la lista de CAs de Mozilla publicada por curl en <https://curl.se/ca/cacert.pem>. Para regenerarlo: descargar ambos, `pip install cryptography`, y `python3 gen_crt_bundle.py --input cacert.pem -q`, copiando el `x509_crt_bundle` resultante a `data/cert/x509_crt_bundle.bin`.
- Se embebe en el firmware vía `board_build.embed_files = data/cert/x509_crt_bundle.bin` en `platformio.ini` (sin filesystem SPIFFS/LittleFS de por medio, igual que la plantilla HTML del portal).
- `src/net/CertBundle.{h,cpp}` centraliza el símbolo `extern` generado por el linker (`_binary_data_cert_x509_crt_bundle_bin_start`) y expone `applyCertBundle(WiFiClientSecure&)`, para no duplicar esa declaración frágil en `WoffuClient` y `OtaUpdater`.

## OTA — detalle de implementación

- URL estable de GitHub Releases: `https://github.com/emanuele-dedonatis/semaforo-woffu/releases/latest/download/firmware.bin` — siempre apunta al asset de la última release, sin necesidad de llamar a la API de GitHub ni parsear JSON.
- Antes de descargar el `.bin` (varios cientos de KB/pocos MB), descargar primero un asset pequeño `version.txt` de la misma release y compararlo con la versión actual (embebida en el firmware) — si coincide, no hace falta bajar el binario entero.
- Validación TLS: certificate bundle, ver `## Certificate bundle (TLS)`.
- La versión actual del firmware se embebe en tiempo de compilación con el build flag `FIRMWARE_VERSION` (p.ej. `-D FIRMWARE_VERSION=\"1.4.0\"`), inyectado por `tools/build_firmware.sh` durante el release (ver `## CI/CD`). En builds locales (`pio run`/`pio run -t upload` sin pasar por CI), `platformio.ini` engancha `extra_scripts = pre:tools/dev_version.py`, que calcula una versión identificable a partir de git — `<último tag con patch+1>-<hash corto>` (p.ej. si el último tag es `v1.1.2` y `HEAD` está en `31a304b`, produce `1.1.3-31a304b`) — y solo si `FIRMWARE_VERSION` no viene ya fijado por `PLATFORMIO_BUILD_FLAGS` (para no pisar ni duplicar el de un build de release). Si no hay tags o `git` no está disponible, no define nada y `Version.h` cae al fallback `"0.0.0-dev"` vía `#ifndef`.
- Tras un flasheo OTA correcto, `HTTPUpdate` deja marcado el nuevo slot como boot y reinicia solo.
- Las URLs `releases/latest/download/*` de GitHub responden con **redirecciones 302** (hacia la release concreta y de ahí al CDN `objects.githubusercontent.com`). Tanto `HTTPClient` como `HTTPUpdate` traen las redirecciones **desactivadas por defecto**, así que hay que activarlas explícitamente con `setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS)` en ambos (comprobación de `version.txt` y descarga de `firmware.bin`); sin esto, `checkAndUpdate()` falla siempre con el código HTTP de la redirección en vez de comprobar la versión real.
- `OtaUpdater::lastErrorDetail()` guarda el motivo concreto del último fallo (código HTTP o `HTTPClient::errorToString()` para la comprobación de versión, `httpUpdate.getLastErrorString()` para la descarga/instalación), que el orquestador vuelca por Serial y en el portal (`ProvisioningPortal::reportOtaStatus()`) — el botón "Comprobar actualización OTA" ya no devuelve un "OK" ciego, sino que la propia página `/` muestra el resultado real de la última comprobación tras recargar.
- `HTTPClient::errorToString(-1)` ("connection refused") es genérico: cubre tanto un fallo de TCP connect como un fallo del handshake TLS (cert bundle, memoria insuficiente, etc.), indistinguibles a ese nivel. Para ese caso concreto se anexa el detalle real vía `WiFiClientSecure::lastError()` (mensaje `mbedtls_strerror`), que sí distingue la causa.

## LED Controller

- GPIOs por defecto: R=25, Y=26, G=27 (libres en un ESP32 DevKit genérico, no son strapping pins). Ajustables en `AppStateMachine.cpp` si el cableado real difiere.
- 3 canales LEDC (PWM), activo-alto (cátodo común, confirmado). La versión del core `arduino-esp32` instalada por PlatformIO usa la API por canal (`ledcSetup`/`ledcAttachPin`/`ledcWrite(channel, duty)`), no la API más nueva por pin (`ledcAttach`) — canales fijos 0/1/2 para R/Y/G.
- `LedTask` interpreta `{color, mode, brightness}` de la cola: `SOLID` (duty fijo), `BLINK_SLOW` (ciclo ~1000ms), `BLINK_FAST` (~250ms, disponible pero sin uso actualmente), `OFF` (duty 0). `color = ALL` enciende los tres a la vez.
- En `UNCONFIGURED` y `PORTAL_WINDOW` el patrón depende solo de si hay alguien conectado a la red WiFi propia (`hasClient()`): `ALL` + `BLINK_SLOW` si no hay nadie, `ALL` + `SOLID` mientras haya alguien conectado.
- Brillo (`brightness` de la config) se aplica como escala del duty cycle cuando el LED está en fase "encendido".

## CI/CD — release y publicación del firmware

Repo: [emanuele-dedonatis/semaforo-woffu](https://github.com/emanuele-dedonatis/semaforo-woffu).

El versionado y la publicación de releases se automatizan con **semantic-release**, apoyándose en los Conventional Commits (ver `CLAUDE.md`), en un workflow de GitHub Actions disparado en push a `main`:

1. `semantic-release` analiza los commits desde el último release. Si no hay `feat`/`fix`/breaking changes (p.ej. solo `docs`/`chore`), no genera release nuevo — no hay build de firmware de por medio.
2. Si toca versión nueva, antes de publicar (plugin `exec`, hook `prepareCmd`) se ejecuta `tools/build_firmware.sh ${nextRelease.version}`, que:
   - escribe `version.txt` con el número de versión calculado;
   - lanza `pio run -e esp32dev` pasando ese número como build flag `FIRMWARE_VERSION` (vía la variable de entorno `PLATFORMIO_BUILD_FLAGS`, para no tocar `platformio.ini` en cada release);
   - copia el binario resultante a `firmware.bin` en la raíz del repo.
3. El plugin `@semantic-release/github` publica el Release con `firmware.bin` y `version.txt` como assets adjuntos automáticamente, dejándolos disponibles en `.../releases/latest/download/...` sin pasos manuales.

Piezas concretas:

- `package.json` — `devDependencies` con `semantic-release` y los plugins usados (`commit-analyzer`, `release-notes-generator`, `exec`, `github`); no se usa `@semantic-release/npm` (no se publica a ningún registro npm).
- `.releaserc.json` — configuración de los plugins anteriores, en el orden en que corren.
- `tools/build_firmware.sh` — script de build descrito arriba, ejecutable también en local para probar un build "de release" sin depender de CI.
- `.github/workflows/release.yml` — job único en `ubuntu-latest`, disparado en push a `main`: checkout con `fetch-depth: 0` (semantic-release necesita el historial completo/tags), setup de Node y de Python+PlatformIO, `npm install`, y `npx semantic-release` con `GITHUB_TOKEN` (el token por defecto del workflow, con permiso `contents: write` para crear el Release).

## Estado

Diseño cerrado, sin preguntas abiertas pendientes.
