# Semáforo Woffu

## Objetivo

Repositorio para publicar y compartir el código fuente de un dispositivo basado en ESP32 ([ELEGOO 3PCS ESP32 Type-C USB](https://amzn.eu/d/0diudeE4)) que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) a través de un semáforo LED ([enlace](https://amzn.eu/d/04LGHEIx)).

Compatible con cualquier placa ESP32 que tenga WiFi (el provisioning y el OTA son solo por WiFi, no se usa Bluetooth).

## Funcionalidad MVP

- Consultar el estado de fichaje en Woffu vía su API y reflejarlo en el semáforo:
  - 🔴 Rojo = no fichado
  - 🟢 Verde = fichado
  - 🟡 Amarillo = estado desconocido (p.ej. no es posible conectarse con Woffu)
- Autenticación con Woffu por **usuario/password** (no API key, ya que solo los admins pueden crearlas).
- Polling adaptativo según franja horaria, para no ser agresivo con la API fuera de las ventanas donde el estado puede cambiar:
  - **Ventanas activas** (entrada y salida, p.ej. 07:30–09:00 y 14:00–18:00, configurables): cada 30-60s. Mismas ventanas para todos los días laborables (lun-vie).
  - **Ventana pasiva** (entre las dos ventanas activas): cada 15 min.
  - **Resto del día / fin de semana**: apagado (tanto el LED como las llamadas a la API).
- Sin reintentos ante fallo de conexión (WiFi o API Woffu): un fallo pasa el LED a ámbar y se reintenta en el siguiente ciclo de polling normal (activo/pasivo), sin backoff adicional.
- Sincronización horaria automática por NTP al arrancar. Zona horaria configurable desde el portal.

## Funcionalidades futuras

- Fichaje automático (p.ej. detectando conexión bluetooth del móvil o del portátil, o por NFC).
- Conectividad con un servidor externo (p.ej. comandos desde Telegram).

## Hardware

- Placa de LEDs de semáforo con 4 pines: GND, R, Y, G (**cátodo común**: R/Y/G se encienden en activo-alto).
- Brillo por PWM, configurable desde el portal.

## OTA

- El dispositivo debe soportar updates de firmware OTA, para poder iterar funcionalidad sin acceso físico.
- **Vía WiFi, descargando desde GitHub Releases** (repo público, sin necesidad de autenticación).
- Comprobación/instalación **bajo demanda**, disparada con un botón en el portal web de configuración (solo disponible durante la ventana de provisioning activa).
- Firmware firmado/verificado: a valorar como mejora posterior (no bloqueante para el MVP).

## Provisioning (portal WiFi)

Pensado para que cualquier compañero pueda configurar su propio dispositivo sin instalar nada: el dispositivo crea su propia red WiFi y sirve una página web de configuración normal, sin apps ni protocolos que aprender. Funciona igual en Android que en iPhone.

Campos configurables desde el portal:

- SSID / password del WiFi
- Usuario / password de Woffu
- Horarios de ventanas activas y pasivas
- Zona horaria
- Brillo de los LEDs
- Forzar ventana activa (para pruebas: ignora horario y fin de semana, ver `## Esquema de configuración` en Arquitectura.md)

La página también muestra la versión de firmware actual (útil para comprobar visualmente que un OTA se aplicó).

### Disponibilidad del portal

- Si el dispositivo **no está configurado**: red WiFi propia (AP) siempre activa, sin límite de tiempo.
- Si el dispositivo **ya está configurado**: AP activo los primeros 10s tras el arranque, en espera de que alguien se conecte a esa red (fijo, no configurable). Mientras no se conecte nadie, pasados esos 10s se desactiva y el dispositivo pasa a funcionamiento normal. Si alguien se conecta, la ventana se mantiene abierta sin límite de tiempo mientras siga conectado, y se cierra en cuanto se desconecta (no hace falta esperar a que expire ningún plazo).
- Mientras el portal está activo (configurado o no), la conexión a la WiFi real todavía no se intenta — se pospone hasta que se cierra la ventana, para no competir con el AP durante la fase de configuración.
- Para volver a entrar en modo configuración: apagar y reencender el dispositivo.

### Seguridad del portal

- Red WiFi propia protegida por password (WPA2), derivada de la MAC address del ESP32 (única y grabada de fábrica), en vez de requerir un paso de fabricación/etiquetado custom.
- Credenciales (WiFi, Woffu) almacenadas en NVS sin cifrar — protegidas solo por la password de la red WiFi propia (ver decisión en Arquitectura.md: cifrado real de NVS descartado para el MVP por la complejidad de flash encryption).

### Feedback visual (parpadeo de LEDs)

Aplica tanto si el dispositivo está configurado (durante los 10s de ventana) como si no (indefinidamente):

| Alguien conectado a la red propia | Patrón |
|---|---|
| No | 3 LEDs parpadeando lento |
| Sí | 3 LEDs fijos |

## Stack de desarrollo

- VSCode + **PlatformIO**, framework **Arduino** (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.
