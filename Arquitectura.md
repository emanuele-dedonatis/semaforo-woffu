# Arquitectura

Basado en [Requisitos.md](Requisitos.md). Documento vivo, igual que el de requisitos: lo que no esté marcado como pregunta abierta es una decisión de diseño tomada.

## Máquina de estados de alto nivel

```
        ┌──────┐
        │ INIT │  (carga config de NVS)
        └──┬───┘
           │
           ▼
    ┌────────────┐
    │ CONNECTING │  (conecta STA, sincroniza hora,
    │ (sin AP    │   comprueba OTA; LEDs rotando
    │  todavia)  │   verde/amarillo/rojo; maximo 20s)
    └──────┬─────┘
           │ wifi+hora listas, o timeout
           ▼
    ┌───────────────┐
    │ PORTAL_WINDOW │  (AP + STA en paralelo; si la STA llegó a
    │               │   conectar, 15s iniciales sin nadie conectado
    └──────┬────────┘   y sin limite si hay alguien; si la STA NO
           │             llegó a conectar, indefinida — nunca pasa
           │             sola a RUNNING)
           │ timeout sin nadie conectado (solo con STA conectada),
           │ o se desconecta el ultimo cliente (solo con STA conectada),
           │ o formulario guardado → [guarda NVS, reboot]
           ▼
      ┌──────────┐
      │ RUNNING  │  (AP apagado, arranca WiFi STA,
      └──────────┘   scheduler + polling Woffu)
```

- **INIT**: lee `Config` de NVS (Preferences).
- **CONNECTING**: arranca la STA (`wifi_.begin()`) pero **todavía no** el AP/portal, tanto si hay credenciales guardadas como si no (sin SSID guardado, `wifi_.begin("", "")` simplemente nunca llega a conectar — incluir este caso en `CONNECTING` en vez de tener un estado `UNCONFIGURED` aparte no cambia el desenlace, ver `PORTAL_WINDOW` más abajo). Intenta conectar a la WiFi guardada, sincronizar la hora por NTP y comprobar actualizaciones OTA (`AppStateMachine::performOtaCheck()`) antes de que nadie pueda ver la página, para que esta se sirva ya con el resultado final en vez de depender de que se auto-refresque mientras el usuario podría estar rellenando el formulario (ver `## OTA`). LEDs rotando (`LedMode::ROTATE`, ver `## LED Controller`) como indicador de "cargando". Pasa a `PORTAL_WINDOW` en cuanto wifi+hora están listas, o a los `kConnectingTimeoutMs` (20s) si no (sin configurar, WiFi inalcanzable, credenciales cambiadas...).
- **PORTAL_WINDOW**: se entra siempre desde `CONNECTING`, con la STA ya conectada, todavía intentándolo, o sin ninguna esperanza de conectar (sin credenciales). Aquí el ESP32 mantiene el AP del portal **y** la WiFi real en paralelo (`WIFI_MODE_APSTA`) — necesario para que la comprobación/actualización OTA del portal (disponible en esta ventana, ver `## OTA`) tenga salida a internet, tanto la que ya se intentó en `CONNECTING` como si la STA conecta más tarde (banderas `otaCheckTriggered_`, ver `## OTA`). El paso automático a `RUNNING` (por timeout de 15s sin nadie conectado, o al desconectarse el último cliente) solo ocurre si `wifi_.isConnected()` es cierto en ese momento: pasar a `RUNNING` sin WiFi dejaría el dispositivo sin WiFi **y** sin portal, sin ninguna forma de reconfigurarse, así que mientras la STA no esté conectada la ventana se queda abierta indefinidamente (se re-evalúa en cada `loop()`, así que en cuanto la STA conecta se cierra en el primer tick sin esperar un nuevo plazo de 15s). Si alguien se conecta, la ventana se queda abierta sin límite de tiempo mientras siga conectado (`WiFi.softAPgetStationNum() > 0`, consultado cada `loop()`) — independientemente del estado de la STA. Al entrar en `RUNNING`, `enterRunning()` solo repite `wifi_.begin()` si la STA no quedó ya conectada desde aquí.
- **RUNNING**: AP y servidor web apagados del todo (para no dejar superficie de ataque innecesaria mientras opera). Arranca la conexión WiFi STA a la red real. Corre el scheduler de polling contra Woffu y refleja el estado en el semáforo.

Al guardar el formulario, el dispositivo persiste en NVS y hace **reboot automático inmediato** (tras responder la página de confirmación HTTP). Si el usuario quiere seguir cambiando cosas, vuelve a entrar en la ventana de portal tras el reboot.

## Tareas y comunicación

Aquí sí que la arquitectura es más simple que con BLE: `WebServer`/`DNSServer` son **síncronos** y se bombean explícitamente desde `loop()` (no crean su propia tarea FreeRTOS como hacía el stack de NimBLE), así que todo corre en la tarea `loopTask` del framework Arduino, salvo el parpadeo de LEDs:

| Tarea | Quién la crea | Responsabilidad |
|---|---|---|
| `loopTask` (la del framework Arduino) | Arduino core | Orquestador: máquina de estados, bombeo de `ProvisioningPortal` (DNS+HTTP), scheduler de polling, llamadas HTTP a Woffu, disparo de OTA |
| `LedTask` | `xTaskCreate` en `setup()` | Animación de LEDs (blink) independiente, para que una llamada HTTP lenta no congele el parpadeo |

Comunicación:

- `ledCmdQueue` (orquestador → LedTask): `{color, mode: OFF|SOLID|BLINK_SLOW|BLINK_FAST}` — sigue siendo una cola FreeRTOS porque cruza tareas de verdad.
- Orquestador ↔ `ProvisioningPortal`: **sin cola** — como los handlers HTTP se ejecutan síncronamente dentro de la misma llamada a `portal_.loop()` (propia tarea `loopTask`), basta con banderas/valores miembro normales (`takeConfigToSave()`, `takeOtaUpdateRequested()`, `takeFactoryResetRequested()`), sin necesidad de mecanismos cross-task. La comprobación OTA es la excepción: no hay bandera porque no la dispara ningún handler HTTP, ver `## OTA`.

### Logging (`Log.{h,cpp}`)

`logPrintf()`/`logPrintln()` son sustitutos directos de `Serial.printf()`/`Serial.println()` — usados en todo el firmware en vez de `Serial` para que cada línea del log lleve un timestamp `[HH:MM:SS]` (hora local, una vez que `TimeSync` ha sincronizado por NTP; ver `kNtpSyncedThreshold` en `TimeSync.h`) o `[+Ns]` (segundos desde el arranque, mientras todavía no hay hora sincronizada — arranque, portal de configuración, fallos de red antes de conectar). Solo `Serial.begin()` en `main.cpp` sigue llamando a `Serial` directamente.

## Estructura del proyecto (PlatformIO)

```
platformio.ini
partitions.csv
src/
  main.cpp                 # setup()/loop(), arranque de tareas
  Log.{h,cpp}               # logPrintf()/logPrintln(): sustituyen a Serial.printf()/println() anteponiendo timestamp
  app/
    AppStateMachine.{h,cpp}   # estados de arriba, orquestación
    Scheduler.{h,cpp}         # ventana encendido/apagado + jornada Woffu → modo activo/pasivo/off
  config/
    Config.{h,cpp}            # struct + wrapper sobre Preferences (NVS)
  net/
    WifiManager.{h,cpp}
    TimeSync.{h,cpp}          # zona horaria por geolocalizacion de IP + NTP (configTime)
    WoffuClient.{h,cpp}       # llamadas a la API de Woffu
    OtaUpdater.{h,cpp}        # HTTPUpdate contra GitHub Releases
    CertBundle.{h,cpp}        # adjunta el bundle de CAs embebido a un WiFiClientSecure
  web/
    ProvisioningPortal.{h,cpp} # AP WiFi + DNS captivo + servidor HTTP con el formulario
  led/
    LedController.{h,cpp}     # LedTask, GPIO digital on/off, patrones de parpadeo
data/
  cert/x509_crt_bundle.bin  # bundle de CAs de Mozilla, vendorizado (ver sección TLS)
```

## Partition table

Se necesitan dos particiones OTA (`app0`/`app1`) + `otadata`. No hace falta SPIFFS/LittleFS (toda la config vive en NVS; la página HTML del portal va embebida como constante en el propio firmware, no como asset servido desde un filesystem). Se usa el esquema `min_spiffs.csv` que trae `arduino-esp32` de serie: dos slots OTA de ~1,9MB cada uno. Tras quitar NimBLE (ver decisión más abajo) el firmware bajó a ~42% de un slot, con margen de sobra incluso para el esquema `default.csv` más ajustado (1,25MB) — se mantiene `min_spiffs.csv` de todas formas, sin urgencia por volver atrás.

## Esquema de configuración (NVS / Preferences)

Namespace `cfg`:

| Key | Tipo | Ejemplo |
|---|---|---|
| `wifi_ssid` | string | |
| `wifi_pass` | string | |
| `woffu_user` | string | |
| `woffu_pass` | string | |
| `win_s`/`win_e` | uint16 (minutos del día) | `450,1140` (07:30–19:00) |
| `force_active` | bool | `false` |

Los ritmos de polling (activo ~60s, pasivo ~900s) ya no son configurables: son constantes fijas en `Scheduler.cpp` (ver `## Cliente Woffu` y `## Scheduler`). El brillo tampoco: los LEDs son GPIO digital on/off, siempre al máximo, ver `## LED Controller`.

**Sin cifrado de NVS para el MVP**: cifrar de verdad el contenido de NVS en el ESP32 requiere activar *flash encryption* a nivel de eFuse (paso de fábrica/primer flasheo, irreversible en modo *release*, y que complica el flujo de OTA con firmas/cifrado de imágenes). Queda descartado para el MVP; la config permanece protegida solo por la password del AP de configuración. Se anota como posible mejora futura opcional para quien quiera más hardening.

## Portal de configuración (WiFi AP + HTTP)

**Decisión revisada**: el diseño original usaba BLE (NimBLE) con un servicio GATT custom. Se descartó tras probarlo en hardware real por ser poco amigable para usuarios no técnicos que van a compartir el dispositivo: apps genéricas como nRF Connect muestran "Unknown Characteristic" para UUIDs custom (no leen el descriptor de nombre para la cabecera), y hay que escribir cada característica una por una entendiendo su formato. Se sustituye por un **portal cautivo WiFi**, el patrón estándar en IoT doméstico (enchufes inteligentes, Chromecast, etc.): sin apps, funciona igual en Android que en iPhone (a diferencia de una alternativa evaluada con Web Bluetooth, que no funciona en Safari/iOS).

### Flujo

1. El dispositivo crea su propia red WiFi (`WiFi.softAP`), SSID `Semaforo-XXXXXX` (últimos 3 bytes de la MAC, igual que antes), protegida con WPA2 y una password de 8 dígitos derivada de la MAC (mismo principio que el PIN de BLE, pero con longitud válida para WPA2-PSK, que exige mínimo 8 caracteres). Justo después, `WiFi.scanNetworks()` escanea las redes WiFi visibles (el AP+STA simultáneo es compatible en el ESP32, ver comentario en `WifiManager::begin()`); el resultado (deduplicado por SSID, ordenado por RSSI descendente) queda cacheado para servir el formulario.
2. Un `DNSServer` interno resuelve **todas** las consultas DNS a la IP propia del AP (`192.168.4.1`), y el `WebServer` redirige (302) cualquier ruta no reconocida a `/`. Combinados, esto dispara la detección automática de "portal cautivo" de Android/iOS/Windows en la mayoría de los casos (se abre solo un navegador con la página de configuración al conectar a la red). Si el sistema operativo no lo detecta automáticamente, `192.168.4.1` siempre funciona navegando a mano.
3. `GET /` sirve un formulario HTML normal (controles nativos: texto, password, `<input type="time">` para las horas — con selector nativo en móvil, `<input type="number">` para los campos numéricos), prellenado con la configuración actual guardada. El campo SSID usa un `<select>` con las redes detectadas en el escaneo del paso 1 (opción con la SSID guardada preseleccionada si sigue estando entre las detectadas): se descartó deliberadamente `<input list>` + `<datalist>` porque el navegador cautivo que abren iOS/Android al conectarse al AP (CNA / popup "Iniciar sesión en red" — el flujo de acceso preferido, no se asume que el usuario abra un navegador normal) es un WebView muy limitado que en iOS normalmente no ejecuta JavaScript y tiene soporte pobre o nulo de `<datalist>`; `<select>` es un control nativo con soporte universal. La página muestra también la versión de firmware actual (`FIRMWARE_VERSION`, ver `## OTA — detalle de implementación`), útil para confirmar visualmente que un OTA se aplicó. Por el mismo motivo, también se imprime por Serial al arrancar (`main.cpp`).
4. `POST /save` guarda todos los campos del formulario a la vez (a diferencia del diseño BLE por característica individual, aquí no hace falta un paso `APPLY_CONFIG` separado — el propio HTML envía todos los campos juntos en cada submit), responde una página de confirmación, y el orquestador persiste en NVS + reinicia.
5. `POST /ota/update` y `POST /factory-reset`: acciones que disparan banderas leídas por el orquestador (igual que el guardado). La comprobación de OTA no tiene ruta propia: la dispara sola el orquestador en cuanto hay WiFi **y** hora sincronizada por NTP, sin depender de que nadie abra `GET /` (ver `## OTA` para el detalle de cómo se muestra el resultado y el progreso sin JavaScript, y por qué hace falta también la hora).

### `ProvisioningPortal` (`src/web/ProvisioningPortal.{h,cpp}`)

- Envuelve `WebServer` (puerto 80) + `DNSServer` (puerto 53), ambos bundled en `arduino-esp32`, sin dependencias externas nuevas.
- `hasClient()` expone `WiFi.softAPgetStationNum() > 0` — usado por el orquestador tanto para el patrón de LED (`ALL SOLID` mientras haya alguien conectado, `BLINK_SLOW` si no) como para decidir cuándo cerrar `PORTAL_WINDOW` (no se cierra por timeout mientras haya alguien conectado; se cierra en el momento en que se desconecta el último cliente).
- `takeConfigToSave()` / `takeOtaCheckRequested()` / `takeOtaUpdateRequested()` / `takeFactoryResetRequested()`: banderas de "un solo uso" (se limpian al leerlas) que el orquestador consulta cada `loop()` — sin colas, ver sección de tareas.
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

### Usuario actual (para saber el `userId`)

```
GET https://app.woffu.com/api/users
Authorization: Bearer <accessToken>
```

Devuelve el perfil completo del usuario logueado; de esa respuesta solo interesa `UserId`, necesario para construir la URL de la jornada del día (ver más abajo). Se pide una única vez por arranque, justo antes de la primera consulta de jornada, y se cachea en RAM junto al `accessToken` (mismo motivo: no persistir en NVS sin cifrar).

### Jornada del día (ventana pasiva de fichaje, fin de semana, festivo)

```
GET https://app.woffu.com/api/svc/core/users/{userId}/diarysummaries/workday
Authorization: Bearer <accessToken>
```

Devuelve el horario habitual de fichaje del día actual (el servidor decide qué es "hoy", igual que en `/signs/slots`):

```json
{
  "startTime": "09:00:00",
  "endTime": "14:00:00",
  "isWeekend": false,
  "isHoliday": false,
  ...
}
```

`startTime`/`endTime` marcan la **ventana pasiva** (tramo en el que se espera que el usuario ya esté fichado, así que el estado cambia poco y basta con comprobarlo cada 15 min); el resto de la ventana de encendido configurada por el usuario es **ventana activa** (cada 30-60s). `isWeekend`/`isHoliday` a `true` apaga el dispositivo entero, sin llamar a `/signs/slots`. Ver `## Scheduler` para la lógica completa y `AppStateMachine::refreshWorkdayInfo()` para el cacheo (una consulta por día, dentro de la ventana de encendido).

### TLS

`app.woffu.com` por HTTPS — usar el certificate bundle de `arduino-esp32` (igual que para OTA) en vez de pinnear certificados. Ver detalle de cómo se genera y embebe en `## Certificate bundle (TLS)`.

## Scheduler

`Scheduler::currentMode()` decide `ACTIVE`/`PASSIVE`/`OFF` combinando, por este orden:

1. `force_active` (config): si está activo, siempre `ACTIVE` — ignora la ventana de encendido y la jornada de Woffu para decidir el modo (para pruebas, sin necesidad de esperar a la franja horaria real).
2. Ventana de encendido/apagado configurada por el usuario (`activeWindow` en `DeviceConfig`): fuera de ella, `OFF` — no se llama ni a `/diarysummaries/workday` ni a `/signs/slots`.
3. Dentro de la ventana de encendido, con la última jornada obtenida de Woffu (`WorkdayInfo`, cacheada por `AppStateMachine` una vez al día, ver arriba): `isWeekend`/`isHoliday` → `OFF`; dentro de `startTime`-`endTime` → `PASSIVE`; fuera → `ACTIVE`.
4. Si todavía no se ha podido obtener la jornada del día (fallo de red/API, o arranque muy reciente), se asume `ACTIVE` — opción más agresiva con la API pero que nunca deja el semáforo apagado por error; se reintenta al día siguiente (mismo criterio de "sin reintentos adicionales" que el resto del cliente Woffu).

`AppStateMachine::refreshWorkdayInfo()` (login + `/users` + `/workday`) se dispara una vez al día tanto dentro de la ventana de encendido como, aparte, siempre que `force_active` esté activo — en este último caso el resultado no cambia el modo (que ya es `ACTIVE` por el punto 1), pero deja constancia en el log de que el flujo completo contra Woffu funciona, útil para probar sin esperar al horario real.

Cada cambio de modo (y el motivo concreto, en castellano) se registra por Serial (`AppStateMachine::handleRunning()`), igual que el resultado de cada consulta de jornada (`refreshWorkdayInfo()`). Los intervalos de poll (`kPollActiveSeconds` ~60s, `kPollPassiveSeconds` ~900s) son constantes en `Scheduler.cpp`, ya no configurables desde el portal.

## Zona horaria (TimeSync)

Para no pedirle al usuario que sepa su TZ POSIX (formato poco intuitivo — de hecho con el signo invertido respecto a lo que se suele escribir, `UTC-2` da hora local UTC+2), `TimeSync::begin()` detecta la zona horaria sola por geolocalización de la IP pública del dispositivo:

```
GET http://ip-api.com/json/?fields=status,message,offset,timezone,city,country
```

HTTP plano (sin TLS): no hay credenciales ni datos sensibles de por medio, así que no hace falta el certificate bundle que sí usan Woffu/GitHub. La respuesta incluye `offset` (el desfase actual respecto a UTC en segundos, ya con el ajuste de horario de verano aplicado si toca — no hace falta resolver reglas de DST a mano) y `timezone`/`city`/`country` solo para dejar constancia legible en el log. Con ese offset se llama a `configTime(offsetSeconds, 0, "pool.ntp.org", "time.nist.gov")` en vez de `configTzTime()` con una cadena TZ fija.

Sin reintentos ante fallo (mismo criterio que el resto del firmware): si la geolocalización falla, se usa offset 0 (UTC) y se reintenta en la siguiente reconexión WiFi — `TimeSync::begin()` se llama cada vez que `AppStateMachine::loop()` detecta una transición a WiFi conectado, no solo en el arranque.

**Dependencia externa nueva**: al igual que OTA depende de GitHub Releases, la zona horaria pasa a depender de la disponibilidad de `ip-api.com` (gratuito, sin API key, límite de 45 peticiones/minuto por IP — muy por encima de lo que puede generar un único dispositivo llamando una vez por reconexión WiFi). Si el servicio cae, el dispositivo sigue funcionando en UTC hasta que vuelva a estar disponible.

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
- La versión actual del firmware se embebe en tiempo de compilación con el build flag `FIRMWARE_VERSION` (p.ej. `-D FIRMWARE_VERSION=\"1.4.0\"`), inyectado por `tools/build_firmware.sh` durante el release (ver `## CI/CD`). En builds locales (`pio run`/`pio run -t upload` sin pasar por CI), `platformio.ini` engancha `extra_scripts = pre:tools/dev_version.py`, que calcula una versión identificable a partir de git — `<último tag con patch+1>-dev.<hash corto>` (identificador de pre-release semver; p.ej. si el último tag es `v1.1.2` y `HEAD` está en `31a304b`, produce `1.1.3-dev.31a304b`) — y solo si `FIRMWARE_VERSION` no viene ya fijado por `PLATFORMIO_BUILD_FLAGS` (para no pisar ni duplicar el de un build de release). Si no hay tags o `git` no está disponible, no define nada y `Version.h` cae al fallback `"0.0.0-dev"` vía `#ifndef`.
- Tras un flasheo OTA correcto, `HTTPUpdate` deja marcado el nuevo slot como boot y reinicia solo.
- Las URLs `releases/latest/download/*` de GitHub responden con **redirecciones 302** (hacia la release concreta y de ahí al CDN `objects.githubusercontent.com`). Tanto `HTTPClient` como `HTTPUpdate` traen las redirecciones **desactivadas por defecto**, así que hay que activarlas explícitamente con `setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS)` en ambos (comprobación de `version.txt` y descarga de `firmware.bin`); sin esto, la comprobación y la descarga fallan siempre con el código HTTP de la redirección en vez de dar el resultado real.
- `OtaUpdater::lastErrorDetail()` guarda el motivo concreto del último fallo (código HTTP o `HTTPClient::errorToString()` para la comprobación de versión, `httpUpdate.getLastErrorString()` para la descarga/instalación), que el orquestador vuelca por Serial y en el portal (`ProvisioningPortal::reportOtaError()`).
- `HTTPClient::errorToString(-1)` ("connection refused") es genérico: cubre tanto un fallo de TCP connect como un fallo del handshake TLS (cert bundle, memoria insuficiente, etc.), indistinguibles a ese nivel. Para ese caso concreto se anexa el detalle real vía `WiFiClientSecure::lastError()` (mensaje `mbedtls_strerror`), que sí distingue la causa.
- **`OtaUpdater` separa comprobar de actualizar**: `checkForUpdate(latestVersionOut)` solo hace el `GET` a `version.txt` (rápido, para poder ofrecerlo como comprobación automática sin comprometerse a descargar nada), y `update(targetVersion)` es quien descarga+flashea la versión indicada (reinicia el dispositivo él solo si tiene éxito, vía `httpUpdate.rebootOnUpdate(true)`) — recibe la versión ya conocida (`ProvisioningPortal::otaTargetVersion()`, la que devolvió la última comprobación) en vez de repetir el `GET` a `version.txt` justo antes de descargar: ese segundo round-trip HTTPS (DNS+TCP+TLS+HTTP completo) era una de las causas de que el progreso tardara varios segundos en aparecer tras pulsar "Actualizar". Ambos métodos comparten el helper privado `fetchLatestVersion()`, que solo usa ya `checkForUpdate()`.
- **Bombeo previo antes de `update()`**: aun sin el round-trip anterior, `httpUpdate.update()` no cede el turno (no llama a nuestro callback de progreso) hasta que empieza a escribir en flash — antes de eso hace su propia conexión TLS y sigue las redirecciones de GitHub hacia el CDN, lo que puede tardar varios segundos sin que el portal responda nada. Como encima el `GET /` que sigue al 302 de `handleOtaUpdate()` todavía no ha llegado cuando `handlePortal()` retoma el control, `AppStateMachine` bombea `portal_.loop()` en un bucle corto (~800ms, `delay(10)` entre vueltas) justo antes de llamar a `update()`, para que esa primera recarga del navegador vea ya "Actualizando: descargando firmware (0%)..." en vez de quedarse sin respuesta — el texto de ese estado avisa además de que puede tardar unos segundos en avanzar, para que nadie refresque a mano ni pulse el botón otra vez mientras `update()` sigue conectando.
- **La página se mantiene "viva" durante la descarga sin JavaScript**: `httpUpdate.update()` bloquea el `loop()` del orquestador (no habría forma de que `portal_.loop()` se ejecutara mientras dura), así que `OtaUpdater::setProgressCallback()` registra un callback que `Update` (`Updater.cpp`) invoca por cada sector de 4KB escrito en flash — muchas veces a lo largo de una descarga de varios cientos de KB. `AppStateMachine` usa ese callback para llamar a `portal_.reportOtaProgress(current, total)` y, justo después, a `portal_.loop()`: es una llamada anidada (mismo hilo, sin tareas ni mutex) que le da al `WebServer` la oportunidad de atender peticiones pendientes en medio de la descarga. Como la UI del portal es HTML puro con `<meta http-equiv="refresh">` (ver más abajo, sin `fetch`/JS), cada recarga automática del navegador durante la descarga llega a buen puerto y sirve un porcentaje de progreso real.
- **La comprobación OTA se dispara antes de abrir el portal, no desde la página**: el camino principal es `AppStateMachine::handleConnecting()` (estado `CONNECTING`, ver `## Máquina de estados`), que llama a `performOtaCheck()` en cuanto `wifi_.isConnected() && timeSync_.isSynced()`, antes de que `PORTAL_WINDOW` abra el AP — así la página se sirve ya con el resultado final. Como red de seguridad para cuando `CONNECTING` agota su margen sin wifi/hora listas, `handlePortal()` repite la misma comprobación mientras el portal ya está abierto, guardando una bandera de "ya disparada" (`otaCheckTriggered_`, una sola vez por arranque venga de donde venga) para no repetirla. Hace falta wifi **y** hora sincronizada por NTP (mismo criterio que `handleRunning()` para llamar a Woffu): sin la hora correcta el reloj del ESP32 sigue en 1970 y la validación del certificado TLS de GitHub falla con un error genérico de handshake (certificado "todavía no válido"). Con el antiguo botón manual esto no se notaba porque el usuario tardaba más en pulsarlo que lo que tarda el NTP en sincronizar; una primera versión de la comprobación automática sí llegó a dispararse antes por mirar solo el WiFi, y falló con ese error.
- **UI del widget de comprobación/actualización sin JavaScript, y sin auto-refresco salvo durante `UPDATING`**: igual que la elección de `<select>` frente a `<datalist>` en el formulario de SSID (`## Provisioning`, `Flujo` paso 3), el navegador cautivo que abren iOS/Android al conectarse al AP puede no ejecutar JavaScript, así que el widget de OTA de `GET /` es HTML puro con `<meta http-equiv="refresh" content="3;url=/">`. `ProvisioningPortal::otaUiState_` (`OtaUiState`: `IDLE`/`CHECKING`/`UP_TO_DATE`/`AVAILABLE`/`UPDATING`/`ERROR`) decide qué renderiza `renderOtaNotice()`; en `UP_TO_DATE` no se muestra nada (la versión instalada ya se ve siempre en `%VERSION%`, no hace falta un aviso aparte para decir que no hay nada que hacer); si hay una versión nueva (`AVAILABLE`) aparece un `<form>` con botón real a `POST /ota/update` (con aviso de no desconectar el dispositivo hasta que los LEDs vuelvan a encenderse, tanto en el `confirm()` del botón como en el texto de `UPDATING`); en `ERROR` solo se muestra el motivo, sin botón de reintentar — para reintentar hay que reiniciar el dispositivo (el arranque siguiente vuelve a disparar la comprobación sola). `otaRefreshMetaFor()` solo mete el `<meta refresh>` en `UPDATING`: un dispositivo recién reseteado (sin WiFi guardada nunca) o con la WiFi guardada inalcanzable se queda indefinidamente en `IDLE`, y si esa página refrescara sola cada 3s borraría a media escritura el formulario de configuración que el usuario esta rellenando justo para arreglarlo (bug real, detectado tras un "Restablecer de fábrica") — `UPDATING` es el único estado en el que el usuario no debería estar a la vez editando ese formulario, así que ahí sí es seguro recargar solo. El estado vive solo en RAM: se resetea a `IDLE` en cada arranque.
- **Feedback de un flasheo con éxito tras el reinicio (no solo por Serial)**: como el propio `update()` reinicia el dispositivo dentro de la llamada, el estado en RAM (`otaUiState_`) se pierde en el reboot y no sirve para esto. `OtaUpdater::setBeforeFlashCallback()` se invoca justo antes de `httpUpdate.update()` (único punto de no retorno antes del reboot) y `AppStateMachine` lo usa para llamar a `Config::markOtaPending(from, to)`, que persiste `ota_from`/`ota_to` en NVS (namespace `cfg`, fuera de `DeviceConfig`). Al arrancar, `AppStateMachine::begin()` consume esa nota una sola vez (`Config::takeOtaNote()`) y, comparando `ota_to` con `FIRMWARE_VERSION` actual, decide si mostrar éxito o "se intentó pero arrancó con otra versión" — vía `ProvisioningPortal::reportOtaStatus()`, un aviso estático independiente del widget dinámico anterior. Si el flasheo falla sin llegar a reiniciar, `handlePortal()` limpia la nota (`Config::clearOtaNote()`) para que no reaparezca en un reinicio posterior sin relación. Limitación conocida: solo es visible si el cliente sigue conectado (o se reconecta a tiempo) durante la ventana de portal del arranque siguiente; si nadie se conecta en esos 15s, el portal se cierra y el mensaje no llega a verse hasta la próxima vez que se abra (para entonces la nota ya se consumió).

## LED Controller

- GPIOs por defecto: R=25, Y=26, G=27 (libres en un ESP32 DevKit genérico, no son strapping pins). Ajustables en `AppStateMachine.cpp` si el cableado real difiere.
- GPIO digital on/off (`pinMode`/`digitalWrite`), activo-alto (cátodo común, confirmado). Sin PWM: al quitarse el brillo configurable (siempre al máximo), un duty LEDC fijo a fondo de escala era equivalente a un simple HIGH/LOW, así que se simplificó a GPIO puro.
- `LedTask` interpreta `{color, mode}` de la cola: `SOLID` (encendido fijo), `BLINK_SLOW` (ciclo ~1000ms), `BLINK_FAST` (~250ms, disponible pero sin uso actualmente), `ROTATE` (turna solo entre verde/amarillo/rojo cada ~1000ms, ignorando el campo `color` del comando — pensado como "cargando"), `OFF` (apagado). `color = ALL` enciende los tres a la vez (no aplica a `ROTATE`).
- `ROTATE` desde el primer `led_.set()` en `AppStateMachine::begin()`, antes incluso de entrar en `CONNECTING`: cubre tambien el escaneo de redes de `portal_.begin()` (unos segundos) una vez se abre `PORTAL_WINDOW`, que si no se quedaria con los LEDs apagados. Mientras dura `CONNECTING` (ver `## Máquina de estados`) sigue rotando mientras se intenta conectar a la WiFi/NTP y comprobar OTA antes de abrir el portal. `updateLedForCurrentState()` lo sustituye por el patron definitivo en cuanto el portal esta listo.
- En `PORTAL_WINDOW` el patrón depende solo de si hay alguien conectado a la red WiFi propia (`hasClient()`): `ALL` + `BLINK_SLOW` si no hay nadie, `ALL` + `SOLID` mientras haya alguien conectado.
- Al entrar en `RUNNING` (`enterRunning()`), `ROTATE` de nuevo: reconectar la STA (si hiciera falta), sincronizar NTP y la primera consulta a Woffu pueden tardar unos segundos, y dejar los LEDs en `OFF` durante ese hueco seria indistinguible del `OFF` legítimo por estar fuera de horario/fin de semana/festivo — confusion detectada en el uso real. `handleRunning()` lo sustituye por el patrón definitivo (`OFF` si `mode == PollMode::OFF`, o el color que toque) en cuanto tiene un resultado real.
- En `RUNNING`, `AppStateMachine::handleRunning()` distingue dos tipos de fallo con el mismo color ámbar: `SOLID` para un fallo puntual de un poll (`ledForStatus(WoffuStatus::UNKNOWN)`, se reintenta solo en el siguiente ciclo) y `BLINK_SLOW` para un fallo persistente que necesita intervención — no conectar a la WiFi configurada tras `kWifiConnectTimeoutMs` (20s desde la entrada a `RUNNING`) o que `WoffuClient::login()` confirme credenciales inválidas (`HTTP_CODE_BAD_REQUEST`/`HTTP_CODE_UNAUTHORIZED`, expuesto como `WoffuClient::credentialsInvalid()`). Ambos casos registran el motivo por Serial la primera vez que se detectan.
- No hay brillo configurable ni compensación de intensidad por canal: los tres LEDs se controlan igual, a todo o nada.

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
