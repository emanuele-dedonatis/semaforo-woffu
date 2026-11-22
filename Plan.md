# Plan de implementación

Seguimiento de qué módulos de [Arquitectura.md](Arquitectura.md) están hechos y cuáles faltan. Documento vivo: al terminar un paso, se mueve de "Pendiente" a "Hecho" (con el commit correspondiente si aplica) como parte del mismo trabajo.

Pensado para poder arrancar cada paso en una conversación nueva: basta con leer este fichero + [Requisitos.md](Requisitos.md) + [Arquitectura.md](Arquitectura.md) para tener todo el contexto de diseño.

## Hecho

- Esqueleto del proyecto PlatformIO (`platformio.ini`, estructura de `src/`).
- `Config` — persistencia en NVS vía `Preferences`, con defaults.
- `LedController` — GPIO digital on/off por canal, patrones `SOLID`/`BLINK_SLOW`/`BLINK_FAST`/`OFF`. (Se probó con PWM/LEDC para compensar el verde más apagado, pero al fijarse el brillo siempre al máximo la compensación quedaba sin efecto — ver Arquitectura.md, `## LED Controller`.)
- `WifiManager` — conexión STA no bloqueante, auto-reconexión.
- `TimeSync` — NTP vía `configTzTime` con la zona horaria configurada.
- `ProvisioningPortal` — portal cautivo WiFi (AP + DNS + formulario HTML), sustituyendo el diseño original por BLE (ver "Decisión revisada" en Arquitectura.md).
- `AppStateMachine` — máquina de estados completa: `CONNECTING` (intenta WiFi/NTP/OTA antes de abrir el portal) / `PORTAL_WINDOW` (ventana de 30s / hasta desconexión si la WiFi conectó, indefinida si no, LEDs parpadeando o fijos según haya alguien conectado, guardado + reboot) y `RUNNING` (arranca WiFi STA, corre `Scheduler` + `WoffuClient` en cada tick, mapea `WoffuStatus` a color de LED, apaga LEDs fuera de ventana/fin de semana).
- `Scheduler` — lógica pura (sin hardware) de encendido/apagado según hora del día y la jornada (fin de semana/festivo) que reporta Woffu.
- `WoffuClient` — login + consulta de estado de fichaje contra la API real, token cacheado en RAM, interpretación del array de `slots`, reintento único de login ante 401.
- `CertBundle` (`src/net/CertBundle.{h,cpp}`) + `data/cert/x509_crt_bundle.bin` — certificate bundle de Mozilla vendorizado y embebido, compartido por `WoffuClient` y `OtaUpdater` (ver Arquitectura.md, `## Certificate bundle (TLS)`).
- `OtaUpdater` — `checkAndUpdate()` real: descarga `version.txt`, compara con `FIRMWARE_VERSION` (build flag embebido en release), y si difiere descarga `firmware.bin` vía `HTTPUpdate` desde GitHub Releases.
- `tools/check_status.py` — script de verificación del flujo de login + estado de fichaje contra la API real de Woffu (login y endpoint confirmados y documentados en Arquitectura.md).
- `tools/toggle_sign.py` — script de verificación del flujo de fichar/desfichar (`POST /api/svc/signs/signs`) contra la API real de Woffu (endpoint y body confirmados y documentados en Arquitectura.md).
- **NFC** (`src/nfc/NfcReader.{h,cpp}`) — lector PN532 por SPI, polling edge-triggered con debounce de retirada. Aprendizaje de una única tarjeta autorizada desde el portal (`POST /nfc/learn`, persistida en NVS vía `Config::setLearnedCardUid()`, sin reboot). Fichaje/desfichaje automático dentro de `PollMode::ACTIVE` (`WoffuClient::toggleSign()`), con feedback de LEDs (`LedMode::ROTATE_FAST` + `BLINK_FAST`) — diseño completo en Arquitectura.md, `## NFC Reader`.
- **Fichaje automático por horario** (`AppStateMachine::handleAutoSign()`/`attemptAutoSign()`) — horario de entrada/salida por día laborable configurable desde el portal (`DeviceConfig::autoSignSchedule`), deshabilitado por defecto. Comprueba el estado real en Woffu antes de fichar (para no fichar dos veces en el sentido contrario), omite festivos/fin de semana según Woffu, y solo intenta cada evento una vez al día — diseño completo en Arquitectura.md, `## Fichaje automático por horario`.
- **Control remoto** (`firmware/src/net/RemoteLink.{h,cpp}` + `server/`) — el dispositivo se conecta por WebSocket (wss) a un servidor propio que sirve una web app: estado, fichar entrada/salida, color fijo persistente (`led_ovr`), refresco, reinicio, OTA, actividad y log en vivo. Repo reorganizado en `firmware/` + `server/`; release solo con cambios en `firmware/`. `platform` fijada a `espressif32 @ ^6.9.0` (la 8.x ya no compila el firmware). Diseño en Arquitectura.md, `## Control remoto`.
- CI/CD (`.github/workflows/release.yml`, `package.json`, `.releaserc.json`, `tools/build_firmware.sh`) — `semantic-release` sobre push a `main`: analiza Conventional Commits, compila el firmware con la versión calculada como build flag, y publica `firmware.bin`/`version.txt` como assets del Release (diseño en Arquitectura.md, `## CI/CD`).

## Pendiente

Nada del MVP. Próximos pasos:

- Flashear en hardware real y validar el flujo completo end-to-end (portal → `RUNNING` → polling → LED → OTA disparado desde el portal → release real via CI que dispare una actualización OTA).
- Validar el módulo PN532 en hardware real: cableado SPI, aprendizaje desde el portal, fichaje/desfichaje con tarjeta aprendida y con una tarjeta distinta, y que `factoryReset()` borra la tarjeta.
- Control remoto: desplegar `server/` en la Raspberry (`server/.env` con el nombre/password reales del dispositivo), aplicar la entrada `semaforo-woffu` del repo `infra` + CNAME `woffu` en IONOS, configurar la URL en el portal del dispositivo y validar en hardware real: conexión/reconexión, memoria libre con dos sesiones TLS a la vez (Woffu + WebSocket), fichaje remoto, color fijo tras reinicio, OTA remota.
- Ver "Futuro" más abajo para alcance post-MVP.

## Futuro (fuera del MVP, ver Requisitos.md)

- Fichaje automático adicional (bluetooth del móvil/portátil).
- Notificaciones (bot Telegram) a partir de los eventos del control remoto.
- Password propia del dispositivo/web app en vez de la derivada de la MAC.
- Migrar a arduino-esp32 3.x (platform 8.x): regenerar el cert bundle con IDF 5 y adaptar `CertBundle`.
- Firmware firmado / flash encryption (hardening opcional, descartado para el MVP).
