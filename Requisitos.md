# Semáforo Woffu

## Objetivo

Repositorio para publicar y compartir el código fuente de un dispositivo basado en ESP32 ([ELEGOO 3PCS ESP32 Type-C USB](https://amzn.eu/d/0diudeE4)) que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) a través de un semáforo LED ([enlace](https://amzn.eu/d/04LGHEIx)).

Compatible con cualquier placa ESP32 que tenga WiFi + BLE (**excluye ESP32-S2**, que no tiene BLE).

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
- Sincronización horaria automática por NTP al arrancar. Zona horaria configurable por BLE.

## Funcionalidades futuras

- Fichaje automático (p.ej. detectando conexión bluetooth del móvil o del portátil, o por NFC).
- Conectividad con un servidor externo (p.ej. comandos desde Telegram).

## Hardware

- Placa de LEDs de semáforo con 4 pines: GND, R, Y, G (**cátodo común**: R/Y/G se encienden en activo-alto).
- Brillo por PWM, configurable por BLE.

## OTA

- El dispositivo debe soportar updates de firmware OTA, para poder iterar funcionalidad sin acceso físico.
- **Vía WiFi, descargando desde GitHub Releases** (repo público, sin necesidad de autenticación). Se descarta OTA vía BLE: el throughput de BLE (pocos KB/s-KB/decenas de KB/s) hace la transferencia de un firmware de 1-2MB lenta y frágil, y requeriría una app custom en vez de una genérica como nRF Connect.
- Comprobación/instalación **bajo demanda**, disparada por comando BLE (solo disponible durante la ventana de provisioning activa).
- Firmware firmado/verificado: a valorar como mejora posterior (no bloqueante para el MVP).

## Provisioning (BLE)

Los usuarios se conectan por bluetooth desde el móvil, a través de una app BLE genérica (p.ej. nRF Connect), para personalizar:

- SSID / password del WiFi
- Usuario / password de Woffu
- Horarios de ventanas activas y pasivas / días de monitorización
- Zona horaria
- Brillo de los LEDs

### Disponibilidad del BLE

- Si el dispositivo **no está configurado**: BLE siempre activo.
- Si el dispositivo **ya está configurado**: BLE activo durante el primer minuto tras el arranque, en espera de que un móvil se empareje (fijo, no configurable). Si nadie se empareja en ese minuto, se desactiva. Si se empareja, la ventana de configuración se extiende hasta que el móvil se desconecte (sin límite de tiempo adicional).
- Para volver a entrar en modo configuración: apagar y reencender el dispositivo.

### Seguridad BLE

- Pairing autenticado mediante **static passkey** (BLE Secure Connections, *Passkey Entry*): el dispositivo exige un PIN fijo de 6 dígitos para completar el pairing (p.ej. `NimBLEDevice::setSecurityPasskey(pin)` en NimBLE-Arduino).
- El PIN se deriva de la MAC address del ESP32 (única y grabada de fábrica), en vez de requerir un paso de fabricación/etiquetado custom.
- Credenciales (WiFi, Woffu) almacenadas en NVS cifrada, nunca en texto plano.

### Feedback visual (parpadeo de LEDs)

| Estado del dispositivo | Móvil conectado | Patrón |
|---|---|---|
| No configurado | No | 3 LEDs parpadeando lento |
| No configurado | Sí (pairing en curso) | 3 LEDs parpadeando rápido |
| Configurado, dentro de ventana BLE post-boot | No | 1 LED (el del estado actual) parpadeando lento |
| Configurado, dentro de ventana BLE post-boot | Sí (pairing en curso) | 1 LED (el del estado actual) parpadeando rápido |

## Stack de desarrollo

- VSCode + **PlatformIO**, framework **Arduino** (`arduino-esp32`).
- Librerías previstas: NimBLE-Arduino (BLE), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.

