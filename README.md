# Semáforo Woffu

Firmware para un dispositivo ESP32 que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED: 🔴 no fichado, 🟢 fichado, 🟡 estado desconocido.

## Hardware

| ESP32 (ELEGOO Type-C, DevKit genérico) | Módulo semáforo LED (R/Y/G) |
|---|---|
| ![Placa ESP32 DevKit con pinout serigrafiado](docs/img/esp32.jpg) | ![Módulo de semáforo LED de 3 colores](docs/img/semaforo.jpg) |

## Configuración

Al arrancar sin datos guardados (o tras un "Restablecer de fábrica"), el dispositivo abre un portal cautivo WiFi (red `Semaforo-XXXXXX`) con un formulario de configuración accesible desde el móvil:

<img src="docs/img/config.png" alt="Portal de configuración desde el móvil" width="300">

- **WiFi SSID / WiFi Password** — red WiFi a la que se conecta el dispositivo en modo normal.
- **Usuario Woffu / Password Woffu** — credenciales de la cuenta de Woffu cuyo estado de fichaje se consulta.
- **Encendido / Apagado** — franja horaria en la que el semáforo está activo y sondea Woffu; fuera de ella los LEDs se apagan (ver `Scheduler` en [Arquitectura.md](Arquitectura.md)).
- **Forzar ventana activa** — ignora el horario configurado y la jornada de Woffu, sondeando siempre cada 60s; pensado para pruebas.
- **Guardar y reiniciar** — persiste la configuración en NVS y reinicia en modo normal (`RUNNING`).
- **Comprobar actualización OTA** — dispara manualmente la comprobación/descarga de actualización, sin esperar al próximo ciclo.
- **Restablecer de fábrica** — borra la configuración guardada y reinicia, volviendo a abrir el portal.

## Documentación

- [Requisitos.md](Requisitos.md) — alcance funcional, hardware, portal de configuración WiFi, OTA.
- [Arquitectura.md](Arquitectura.md) — máquina de estados, tareas, portal WiFi/NVS, cliente Woffu, OTA.
- [Plan.md](Plan.md) — plan de implementación: pasos hechos y pendientes.
- [tools/README.md](tools/README.md) — scripts auxiliares de desarrollo.

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.
- CI/CD: GitHub Actions + `semantic-release` (versionado y publicación de releases a partir de Conventional Commits).

## Ejemplo de log por serie

Primer arranque: se abre el portal de configuración, se detecta la zona horaria por geolocalización IP y se sincroniza la hora por NTP.

```
[+0s] Semaforo Woffu - firmware 1.1.3-dev.9f12992
[+0s] Ventana de portal de configuracion abierta (10s, o hasta que se desconecte el cliente).
[+0s] Portal WiFi: Semaforo-A1B2C3, password: 74019283 (192.168.4.1)
[+0s] WiFi conectado, IP: 192.168.22.24. Detectando zona horaria y sincronizando hora por NTP...
[+1s] Geolocalizacion IP: GET http://ip-api.com/json/?fields=status,message,offset,timezone,city,country -> 200
[+1s] Geolocalizacion IP: Valencia, Spain (Europe/Madrid), UTC+120 min
[+1s] Cliente conectado al portal de configuracion.
[08:39:01] Hora sincronizada por NTP.
[08:40:07] Cliente desconectado del portal de configuracion.
[08:40:07] Ventana de portal cerrada: se desconecto el ultimo cliente. Pasando a modo normal.
[08:40:07] Modo normal (RUNNING): portal apagado, conectando a la WiFi configurada.
[08:40:09] Woffu API: POST https://app.woffu.com/api/svc/accounts/authorization/token -> 200
[08:40:11] Woffu API: GET https://app.woffu.com/api/users -> 200
[08:40:13] Woffu API: GET https://app.woffu.com/api/svc/core/users/1234567/diarysummaries/workday -> 200
[08:40:13] Woffu: jornada de hoy - ventana pasiva 09:00-14:00, fin de semana=si, festivo=no.
[08:40:13] Cambio de ventana de polling: off (hoy es fin de semana segun Woffu)
```

Día laborable normal: el dispositivo arranca ya en modo `RUNNING`, sondea Woffu a la espera del fichaje (ventana activa, cada 60s), lo detecta, y pasa a sondear más despacio (ventana pasiva, cada 15 min) mientras dura la jornada según Woffu.

```
[+0s] Semaforo Woffu - firmware 1.1.3-dev.9f12992
[07:30:02] Modo normal (RUNNING): portal apagado, conectando a la WiFi configurada.
[07:30:04] Woffu API: POST https://app.woffu.com/api/svc/accounts/authorization/token -> 200
[07:30:06] Woffu API: GET https://app.woffu.com/api/users -> 200
[07:30:08] Woffu API: GET https://app.woffu.com/api/svc/core/users/1234567/diarysummaries/workday -> 200
[07:30:08] Woffu: jornada de hoy - ventana pasiva 09:00-18:00, fin de semana=no, festivo=no.
[07:30:08] Cambio de ventana de polling: activa (fuera de la ventana pasiva de fichaje de Woffu)
[07:30:08] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[07:30:08] Estado Woffu: NO FICHADO (rojo)
...
[08:57:12] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[08:57:12] Estado Woffu: FICHADO (verde)
[09:00:08] Cambio de ventana de polling: pasiva (dentro de la ventana pasiva de fichaje de Woffu)
[09:00:08] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[09:00:08] Estado Woffu: FICHADO (verde)
[09:15:08] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[09:15:08] Estado Woffu: FICHADO (verde)
...
[18:00:08] Cambio de ventana de polling: activa (fuera de la ventana pasiva de fichaje de Woffu)
[18:00:08] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[18:00:08] Estado Woffu: FICHADO (verde)
[18:03:41] Woffu API: GET https://app.woffu.com/api/svc/signs/v2/signs/slots -> 200
[18:03:41] Estado Woffu: NO FICHADO (rojo)
```
