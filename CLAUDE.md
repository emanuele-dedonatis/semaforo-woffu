# CLAUDE.md

Guía para Claude Code al trabajar en este repositorio.

## Qué es este proyecto

Firmware para un dispositivo ESP32 que muestra el estado de fichaje de [Woffu](https://woffu.com/) mediante un semáforo LED (rojo/verde/ámbar). Ver [Requisitos.md](Requisitos.md) para el detalle funcional completo (MVP, hardware, OTA, provisioning BLE, seguridad).

## Stack

- PlatformIO + framework Arduino (`arduino-esp32`).
- Librerías previstas: NimBLE-Arduino (BLE), HTTPClient/HTTPUpdate (API Woffu y OTA), Preferences (NVS), ArduinoJson.

## Mantenimiento de la documentación

`README.md` y este `CLAUDE.md` deben mantenerse siempre actualizados: cualquier cambio relevante en stack, estructura del proyecto o alcance debe reflejarse en ambos como parte del mismo trabajo, no como tarea aparte.

## Convenciones de commits

Este repo usa **Conventional Commits** de forma estricta, para poder automatizar versionado (semantic-release o similar) a partir del historial. Formato:

```
<tipo>(<scope opcional>): <descripción>
```

Tipos habituales: `feat`, `fix`, `docs`, `refactor`, `chore`, `test`, `build`, `ci`. Usa `!` o un footer `BREAKING CHANGE:` para cambios incompatibles.
