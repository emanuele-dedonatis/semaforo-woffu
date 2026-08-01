# Plan de implementación

Seguimiento de qué módulos de [Arquitectura.md](Arquitectura.md) están hechos y cuáles faltan. Documento vivo: al terminar un paso, se mueve de "Pendiente" a "Hecho" (con el commit correspondiente si aplica) como parte del mismo trabajo.

Pensado para poder arrancar cada paso en una conversación nueva: basta con leer este fichero + [Requisitos.md](Requisitos.md) + [Arquitectura.md](Arquitectura.md) para tener todo el contexto de diseño.

## Hecho

- Esqueleto del proyecto PlatformIO (`platformio.ini`, estructura de `src/`).
- `Config` — persistencia en NVS vía `Preferences`, con defaults.
- `LedController` — PWM real por canal LEDC, patrones `SOLID`/`BLINK_SLOW`/`BLINK_FAST`/`OFF`, compensación de brillo por canal (el verde necesita más duty que rojo/amarillo por su mayor Vf, con la misma resistencia de serie del módulo).
- `WifiManager` — conexión STA no bloqueante, auto-reconexión.
- `TimeSync` — NTP vía `configTzTime` con la zona horaria configurada.
- `ProvisioningPortal` — portal cautivo WiFi (AP + DNS + formulario HTML), sustituyendo el diseño original por BLE (ver "Decisión revisada" en Arquitectura.md).
- `AppStateMachine` — máquina de estados completa para `UNCONFIGURED` / `PORTAL_WINDOW` (ventana de 30s / hasta desconexión, LEDs parpadeando o fijos según haya alguien conectado, guardado + reboot). `RUNNING` solo arranca el WiFi STA y apaga LEDs — el bucle real de polling todavía no existe (ver pendientes).
- `tools/check_status.py` — script de verificación del flujo de login + estado de fichaje contra la API real de Woffu (login y endpoint confirmados y documentados en Arquitectura.md).

## Pendiente (orden sugerido)

1. **`Scheduler`** — lógica pura (sin hardware) de ventanas activa/pasiva/off según hora del día y `DeviceConfig`, devolviendo el intervalo de poll aplicable. Fácil de probar sin flashear.
2. **`WoffuClient`** — implementar login + consulta de estado siguiendo el diseño ya cerrado en Arquitectura.md (`## Cliente Woffu`): token cacheado en RAM, interpretación del array de `slots`.
3. **Bucle real de `RUNNING` en `AppStateMachine`** — usar `Scheduler` + `WoffuClient` en cada tick para decidir cuándo pollear, mapear `WoffuStatus` a color de LED (rojo/verde/ámbar en fallo), y apagar LEDs fuera de ventana/fin de semana.
4. **`OtaUpdater`** — implementar `checkAndUpdate()` de verdad: descarga `version.txt` + `firmware.bin` desde GitHub Releases (URL ya definida en Arquitectura.md), certificate bundle para TLS.
5. **CI/CD** — `.github/workflows/release.yml`: `semantic-release` + build de PlatformIO + adjuntar `firmware.bin`/`version.txt` al Release (diseño ya cerrado en Arquitectura.md, sección `## CI/CD`).

## Futuro (fuera del MVP, ver Requisitos.md)

- Fichaje automático (bluetooth del móvil/portátil, o NFC).
- Conectividad con servidor externo (p.ej. comandos desde Telegram).
- Firmware firmado / flash encryption (hardening opcional, descartado para el MVP).
