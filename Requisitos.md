# Semáforo Woffu

## Objetivo

Repositorio para publicar y compartir el código fuente de un dispositivo basado en ESP32 ([ELEGOO 3PCS ESP32 Type-C USB](https://amzn.eu/d/0diudeE4)) que muestra visualmente el estado de fichaje de [Woffu](https://woffu.com/) a través de un semáforo LED ([enlace](https://amzn.eu/d/04LGHEIx)).

Compatible con cualquier placa ESP32 que tenga WiFi (el provisioning y el OTA son solo por WiFi, no se usa Bluetooth).

## Funcionalidad MVP

- Consultar el estado de fichaje en Woffu vía su API y reflejarlo en el semáforo:
  - 🔴 Rojo = no fichado
  - 🟢 Verde = fichado
  - 🟡 Amarillo fijo = estado desconocido por un fallo puntual (p.ej. la API de Woffu no responde en un poll concreto)
  - 🟡 Amarillo parpadeando = fallo persistente que requiere revisar la configuración: no se ha podido conectar a la WiFi configurada, o las credenciales de Woffu son incorrectas
  - Justo al cerrarse la ventana de configuración y pasar a modo normal, los LEDs rotan (verde→amarillo→rojo, ver `### Feedback visual`) mientras se reconecta la WiFi, se sincroniza la hora y se hace la primera consulta a Woffu: sin esto, ese hueco se veía igual que "apagado por estar fuera de horario", confundiendo ambos casos.
- Autenticación con Woffu por **usuario/password** (no API key, ya que solo los admins pueden crearlas).
- Polling adaptativo, para no ser agresivo con la API fuera de las ventanas donde el estado puede cambiar. Para simplificar la configuración, el usuario solo indica una **ventana de encendido/apagado** (por defecto 07:30–19:00, configurable); dentro de ella, el propio dispositivo consulta a Woffu la jornada del día (`GET /api/svc/core/users/{userId}/diarysummaries/workday`) para decidir el ritmo, sin que haga falta configurar nada más:
  - **Ventana pasiva** (el tramo `startTime`–`endTime` que devuelve Woffu para el día, la franja de fichaje habitual): cada 15 min.
  - **Resto de la ventana de encendido** (fuera de ese tramo, p.ej. antes de entrar o después de salir): cada minuto. Excepción: si ya se ha detectado el fichaje esperado en ese tramo (entrada ya fichada antes de la ventana pasiva, o salida ya fichada después de ella), se relaja también a cada 15 min hasta el siguiente cambio de ventana, ya que no hace falta reaccionar rápido a un cambio que ya ocurrió.
  - **Fuera de la ventana de encendido, fin de semana o festivo** (`isWeekend`/`isHoliday` de la respuesta de Woffu): apagado (tanto el LED como las llamadas a la API de fichaje). Si por algún fallo no se puede consultar la jornada del día, se asume ventana activa (más agresiva pero segura) hasta el día siguiente.
  - Todas estas decisiones (por qué se apaga, por qué pasa a pasiva/activa) se registran de forma explicativa por el puerto serie.
- Sin reintentos ante fallo de conexión (WiFi o API Woffu): un fallo pasa el LED a ámbar y se reintenta en el siguiente ciclo de polling normal (activo/pasivo), sin backoff adicional. Excepción: si no se consigue conectar a la WiFi configurada, o si Woffu rechaza las credenciales configuradas (usuario/password incorrectos), se registra un error por el puerto serie y el LED pasa a ámbar parpadeando en vez de fijo, para distinguir un problema de configuración persistente de un fallo puntual.
- Sincronización horaria automática por NTP al arrancar. Zona horaria detectada automáticamente por geolocalización de la IP pública (sin campo que configurar en el portal, ver `## Cliente Woffu`/`TimeSync` en Arquitectura.md).

## Funcionalidades futuras

- Fichaje automático (p.ej. detectando conexión bluetooth del móvil o del portátil, o por NFC).
- Conectividad con un servidor externo (p.ej. comandos desde Telegram).

## Hardware

- Placa de LEDs de semáforo con 4 pines: GND, R, Y, G (**cátodo común**: R/Y/G se encienden en activo-alto).
- LEDs controlados por GPIO digital (on/off), siempre al máximo — ya no es configurable desde el portal.

## OTA

- El dispositivo debe soportar updates de firmware OTA, para poder iterar funcionalidad sin acceso físico.
- **Vía WiFi, descargando desde GitHub Releases** (repo público, sin necesidad de autenticación).
- **Comprobación automática**, sin necesidad de que el usuario haga nada, en cuanto el dispositivo tiene WiFi y hora sincronizada mientras el portal está activo (no depende de que se abra la página). La página muestra si el firmware está al día o si hay una versión nueva; **instalación bajo demanda**, con un botón que solo aparece cuando hay una actualización disponible (solo durante la ventana de provisioning activa). Si la comprobación falla, no hay reintento automático ni botón: hay que reiniciar el dispositivo.
- Firmware firmado/verificado: a valorar como mejora posterior (no bloqueante para el MVP).

## Provisioning (portal WiFi)

Pensado para que cualquier compañero pueda configurar su propio dispositivo sin instalar nada: el dispositivo crea su propia red WiFi y sirve una página web de configuración normal, sin apps ni protocolos que aprender. Funciona igual en Android que en iPhone.

Campos configurables desde el portal:

- SSID / password del WiFi
- Usuario / password de Woffu
- Horario de encendido/apagado
- Forzar ventana activa (para pruebas: ignora horario y la jornada que reporta Woffu, ver `## Esquema de configuración` en Arquitectura.md)

La página también muestra la versión de firmware actual (útil para comprobar visualmente que un OTA se aplicó).

### Disponibilidad del portal

- Al arrancar (esté o no configurado el dispositivo — sin credenciales guardadas el intento de conexión simplemente nunca llega a completarse), antes de abrir el AP intenta conectar a la WiFi guardada, sincronizar la hora y comprobar actualizaciones OTA (LEDs rotando como indicador de carga, máximo 20s de margen — ver `### Feedback visual` más abajo).
- Hecho eso, el AP se activa. Si la WiFi llegó a conectar, se activa solo los primeros 15s, en espera de que alguien se conecte a esa red (fijo, no configurable); mientras no se conecte nadie, pasados esos 15s se desactiva y el dispositivo pasa a funcionamiento normal. Si alguien se conecta, la ventana se mantiene abierta sin límite de tiempo mientras siga conectado, y se cierra en cuanto se desconecta. Si la WiFi **no** llegó a conectar (no configurada, credenciales incorrectas, red no disponible...), el AP se queda activo indefinidamente sin pasar nunca a funcionamiento normal: no tendría sentido, porque ahí el dispositivo se quedaría sin WiFi y sin portal, sin ninguna forma de reconfigurarse.
- Mientras el portal está activo, el ESP32 mantiene el AP **y** la WiFi real en paralelo (`WIFI_MODE_APSTA`) — necesario para que la comprobación/actualización OTA del propio portal tenga salida a internet, tanto en ese intento inicial como si conecta más tarde.
- Para volver a entrar en modo configuración: apagar y reencender el dispositivo.

### Seguridad del portal

- Red WiFi propia protegida por password (WPA2), derivada de la MAC address del ESP32 (única y grabada de fábrica), en vez de requerir un paso de fabricación/etiquetado custom.
- Credenciales (WiFi, Woffu) almacenadas en NVS sin cifrar — protegidas solo por la password de la red WiFi propia (ver decisión en Arquitectura.md: cifrado real de NVS descartado para el MVP por la complejidad de flash encryption).

### Feedback visual (parpadeo de LEDs)

Desde que se enciende el dispositivo hasta que el portal está realmente listo: los 3 LEDs van rotando, 1s cada uno, verde → amarillo → rojo → repite (indicador de "cargando"). Esta fase incluye intentar conectar a la WiFi, sincronizar la hora y comprobar OTA antes de abrir el portal (máximo 20s de margen).

Una vez abierto el portal, aplica tanto si la ventana es de 15s (WiFi conectada) como si es indefinida (sin WiFi):

| Alguien conectado a la red propia | Patrón |
|---|---|
| No | 3 LEDs parpadeando lento |
| Sí | 3 LEDs fijos |

Al cerrarse la ventana de portal (o al terminar el "cargando" inicial si nadie se conecta), y hasta la primera consulta real a Woffu: los LEDs vuelven a rotar (mismo patrón que al arrancar), para no confundirlo con estar apagado por horario.

## Stack de desarrollo

- VSCode + **PlatformIO**, framework **Arduino** (`arduino-esp32`).
- Librerías: WebServer y DNSServer (portal de configuración, incluidas en el core de `arduino-esp32`), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.
